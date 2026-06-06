// Pure formatters for the SignalK dashboard. Extracted from
// signalk_dashboard.c so on-device tests can exercise them without an
// LVGL context.

#include "signalk_dashboard_fmt.h"
#include <math.h>
#include <stdio.h>

static const double MS_TO_KT   = 1.943844;
static const double RAD_TO_DEG = 57.295779513;
static const double M_TO_FT    = 3.280840;

void fmt_heading_to(char *buf, size_t n, const signalk_value_t *v, bool fresh) {
    if (!fresh || !v) { snprintf(buf, n, "—"); return; }
    double deg = v->value * RAD_TO_DEG;
    while (deg < 0)    deg += 360.0;
    while (deg >= 360) deg -= 360.0;
    snprintf(buf, n, "%03.0f°", deg);
}

void fmt_depth_ft_to(char *buf, size_t n, const signalk_value_t *v, bool fresh) {
    if (!fresh || !v) { snprintf(buf, n, "—"); return; }
    snprintf(buf, n, "%.1f ft", v->value * M_TO_FT);
}

void fmt_sog_kt_to(char *buf, size_t n, const signalk_value_t *v, bool fresh) {
    if (!fresh || !v) { snprintf(buf, n, "—"); return; }
    snprintf(buf, n, "%.1f kt", v->value * MS_TO_KT);
}

void fmt_wind_to(char *buf, size_t n,
                 const signalk_value_t *ang, bool ang_fresh,
                 const signalk_value_t *spd, bool spd_fresh) {
    if (!ang_fresh && !spd_fresh) { snprintf(buf, n, "—"); return; }

    char angle_buf[12] = "—";
    if (ang_fresh && ang) {
        double deg = ang->value * RAD_TO_DEG;
        while (deg <= -180) deg += 360.0;
        while (deg >   180) deg -= 360.0;
        if (fabs(deg) < 0.5)        snprintf(angle_buf, sizeof(angle_buf), "0°");
        else if (fabs(deg - 180.0) < 0.5 || fabs(deg + 180.0) < 0.5)
                                     snprintf(angle_buf, sizeof(angle_buf), "180°");
        else if (deg > 0)            snprintf(angle_buf, sizeof(angle_buf), "%.0f° S", deg);
        else                         snprintf(angle_buf, sizeof(angle_buf), "%.0f° P", -deg);
    }
    char speed_buf[12] = "—";
    if (spd_fresh && spd) snprintf(speed_buf, sizeof(speed_buf), "%.0f kt", spd->value * MS_TO_KT);

    snprintf(buf, n, "%s / %s", angle_buf, speed_buf);
}
