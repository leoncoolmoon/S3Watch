#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

// done: called with a heap-free, NUL-terminated "A.B.C.D" string (lifetime
// bounded to this callback — copy if you need to keep it).
typedef void (*ip_picker_done_cb_t)(const char *ip_str, void *user);
typedef void (*ip_picker_cancel_cb_t)(void *user);

// Build a four-roller IPv4 picker into `parent`. `initial` may be NULL or
// any string; if it parses as IPv4 each octet pre-selects, otherwise all
// rollers start at 0. Swipe-right cancels.
void ip_picker_create(lv_obj_t *parent,
                      const char *initial,
                      ip_picker_done_cb_t   done,
                      ip_picker_cancel_cb_t cancel,
                      void *user);

#ifdef __cplusplus
}
#endif
