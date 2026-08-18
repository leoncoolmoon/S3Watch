// Pure calculator state machine — no LVGL or ESP-IDF dependencies.
// Extracted from calculator_app.c so host tests can exercise it directly.

#include "calc_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static double    s_a         = 0.0;
static double    s_b         = 0.0;   // remembered right operand for chained =
static calc_op_t s_op        = OP_NONE;
static char      s_disp[24]  = "0";
static bool      s_new_input = true;
static bool      s_has_dot   = false;
static bool      s_eq_last   = false; // last action was =
static bool      s_error     = false;

static double apply_op(double a, double b, calc_op_t op)
{
    switch (op) {
    case OP_ADD: return a + b;
    case OP_SUB: return a - b;
    case OP_MUL: return a * b;
    case OP_DIV: return (b != 0.0) ? a / b : NAN;
    default:     return b;
    }
}

static void fmt(double v, char *out, size_t sz)
{
    if (!isfinite(v)) { snprintf(out, sz, "Error"); return; }
    double ip;
    if (modf(v, &ip) == 0.0 && fabs(v) < 1e12)
        snprintf(out, sz, "%.0f", v);
    else
        snprintf(out, sz, "%.10g", v);
}

void calc_reset(void)
{
    s_a = s_b = 0.0;
    s_op        = OP_NONE;
    s_error     = false;
    strcpy(s_disp, "0");
    s_new_input = true;
    s_has_dot   = false;
    s_eq_last   = false;
}

void calc_digit(int d)
{
    if (s_error) return;
    if (s_eq_last) { s_a = 0.0; s_op = OP_NONE; }
    s_eq_last = false;
    if (s_new_input) {
        snprintf(s_disp, sizeof(s_disp), "%d", d);
        s_has_dot   = false;
        s_new_input = false;
    } else {
        if (strlen(s_disp) >= 15) return;
        if (strcmp(s_disp, "0") == 0 && d != 0) {
            snprintf(s_disp, sizeof(s_disp), "%d", d);
            return;
        }
        if (strcmp(s_disp, "0") == 0 && d == 0) return;
        size_t n = strlen(s_disp);
        s_disp[n]   = (char)('0' + d);
        s_disp[n+1] = '\0';
    }
}

void calc_dot(void)
{
    if (s_error) return;
    s_eq_last = false;
    if (s_new_input) {
        strcpy(s_disp, "0.");
        s_has_dot   = true;
        s_new_input = false;
    } else if (!s_has_dot && strlen(s_disp) < 15) {
        strcat(s_disp, ".");
        s_has_dot = true;
    }
}

void calc_clear(void)
{
    if (s_error || strcmp(s_disp, "0") == 0) {
        s_a = s_b = 0.0;
        s_op    = OP_NONE;
        s_error = false;
    }
    strcpy(s_disp, "0");
    s_has_dot   = false;
    s_new_input = true;
    s_eq_last   = false;
}

void calc_sign(void)
{
    if (s_error) return;
    double v = atof(s_disp);
    if (v != 0.0) {
        fmt(-v, s_disp, sizeof(s_disp));
        s_has_dot = (strchr(s_disp, '.') != NULL);
    }
}

void calc_pct(void)
{
    if (s_error) return;
    double v = atof(s_disp) / 100.0;
    fmt(v, s_disp, sizeof(s_disp));
    s_has_dot = (strchr(s_disp, '.') != NULL);
}

void calc_op(calc_op_t op)
{
    if (s_error) return;
    if (s_op != OP_NONE && !s_new_input && !s_eq_last) {
        double cur = atof(s_disp);
        double res = apply_op(s_a, cur, s_op);
        if (!isfinite(res)) {
            strcpy(s_disp, "Error"); s_error = true; s_op = OP_NONE; return;
        }
        s_a = res;
        fmt(res, s_disp, sizeof(s_disp));
        s_has_dot = (strchr(s_disp, '.') != NULL);
    } else if (!s_eq_last) {
        s_a = atof(s_disp);
    }
    // if s_eq_last: s_a already holds the result — chain the new op
    s_op        = op;
    s_new_input = true;
    s_eq_last   = false;
}

void calc_eq(void)
{
    if (s_error) return;
    if (s_op == OP_NONE && !s_eq_last) return;
    double cur = s_eq_last ? s_b : atof(s_disp);
    if (!s_eq_last) s_b = cur;
    double res = apply_op(s_a, cur, s_op);
    if (!isfinite(res)) {
        strcpy(s_disp, "Error"); s_error = true;
        s_op = OP_NONE; s_eq_last = false;
        return;
    }
    s_a = res;
    fmt(res, s_disp, sizeof(s_disp));
    s_has_dot   = (strchr(s_disp, '.') != NULL);
    s_new_input = true;
    s_eq_last   = true;
}

const char *calc_display(void)
{
    return s_disp;
}

void calc_get_expr(char *buf, size_t n)
{
    if (s_op != OP_NONE && !s_eq_last) {
        static const char *sym[] = {"", "+", "-", "\xc3\x97", "\xc3\xb7"};
        char a_str[24];
        fmt(s_a, a_str, sizeof(a_str));
        snprintf(buf, n, "%s %s", a_str, sym[s_op]);
    } else {
        if (n > 0) buf[0] = '\0';
    }
}

bool calc_is_error(void)
{
    return s_error;
}
