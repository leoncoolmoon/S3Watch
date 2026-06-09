#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { OP_NONE=0, OP_ADD, OP_SUB, OP_MUL, OP_DIV } calc_op_t;

// Reset all calculator state to initial (display "0").
void        calc_reset(void);

// Button press handlers — each corresponds to one button type.
void        calc_digit(int d);      // 0-9
void        calc_dot(void);
void        calc_clear(void);       // C: clears current input; double-C clears all state
void        calc_sign(void);        // ±
void        calc_pct(void);         // %
void        calc_op(calc_op_t op);  // +, -, ×, ÷
void        calc_eq(void);          // =

// Query the current display string (the main number line).
const char *calc_display(void);

// Write the expression line (e.g. "5 +" while entering second operand) into buf.
// Empty string when no pending operation or after =.
void        calc_get_expr(char *buf, size_t n);

// True while the calculator is in error state (e.g. divide-by-zero).
bool        calc_is_error(void);

#ifdef __cplusplus
}
#endif
