#pragma once
// Pure-C helper for parsing SignalK notification state strings into the
// project's signalk_alert_state_t enum. No ESP-IDF deps — included by
// both the firmware and the host unit tests (see tests/).

#include <string.h>
#include "signalk_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// SignalK notification states, ascending severity:
//   "nominal", "normal" → NORMAL (not active; never displayed)
//   "alert"             → ALERT
//   "warn"              → WARN
//   "alarm"             → ALARM
//   "emergency"         → EMERGENCY
// Anything else (NULL, unknown string) → NORMAL.
static inline signalk_alert_state_t signalk_alert_parse_state(const char *s) {
    if (!s) return SIGNALK_ALERT_NORMAL;
    if (strcmp(s, "emergency") == 0) return SIGNALK_ALERT_EMERGENCY;
    if (strcmp(s, "alarm")     == 0) return SIGNALK_ALERT_ALARM;
    if (strcmp(s, "warn")      == 0) return SIGNALK_ALERT_WARN;
    if (strcmp(s, "alert")     == 0) return SIGNALK_ALERT_ALERT;
    return SIGNALK_ALERT_NORMAL;
}

#ifdef __cplusplus
}
#endif
