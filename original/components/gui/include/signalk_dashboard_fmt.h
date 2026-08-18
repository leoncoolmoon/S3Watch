#pragma once
// SignalK dashboard value formatters. Pure functions: SI in, display
// string out. No LVGL, no timers — staleness is decided by the caller and
// passed in as the `fresh` bool. Tests exercise these directly.

#include <stdbool.h>
#include <stddef.h>
#include "signalk_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// Heading: rad → "045°" (zero-padded 3 digits) or "—" when !fresh.
void fmt_heading_to(char *buf, size_t n, const signalk_value_t *v, bool fresh);

// Depth: meters → "40.4 ft" (American nautical), or "—" when !fresh.
void fmt_depth_ft_to(char *buf, size_t n, const signalk_value_t *v, bool fresh);

// SOG: m/s → "5.4 kt", or "—" when !fresh.
void fmt_sog_kt_to(char *buf, size_t n, const signalk_value_t *v, bool fresh);

// Apparent wind: angle rad + speed m/s → "45° S / 12 kt" (port/starboard).
// "—" if both angle and speed are stale.
void fmt_wind_to(char *buf, size_t n,
                 const signalk_value_t *ang, bool ang_fresh,
                 const signalk_value_t *spd, bool spd_fresh);

#ifdef __cplusplus
}
#endif
