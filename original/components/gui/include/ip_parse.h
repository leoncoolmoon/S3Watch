#pragma once
// Pure-C helper, no ESP-IDF / LVGL deps — included by both the firmware
// and the host unit tests (see tests/).

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Parse "A.B.C.D" into 4 ints in [0, 255]. Returns false on any malformed
// octet, trailing garbage, leading whitespace, or NULL input.
static inline bool ip_parse_v4(const char *s, int out[4]) {
    if (!s) return false;
    int a, b, c, d;
    char tail = 0;
    int n = sscanf(s, "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail);
    if (n != 4) return false;
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) return false;
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
    return true;
}

#ifdef __cplusplus
}
#endif
