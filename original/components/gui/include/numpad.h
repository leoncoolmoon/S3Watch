#pragma once
#include "lvgl.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*numpad_done_cb_t)(uint32_t value, void *user);
typedef void (*numpad_cancel_cb_t)(void *user);

// Build a numeric input screen: title + big number label + 3x4 grid
// (1 2 3 / 4 5 6 / 7 8 9 / backspace 0 done). Swipe-right cancels.
// Digit presses are dropped when the result would exceed `max_value`.
// Done button is disabled while value < min_value or value > max_value.
void numpad_create(lv_obj_t *parent,
                   const char *title,
                   uint32_t initial,
                   uint32_t min_value,
                   uint32_t max_value,
                   numpad_done_cb_t   done,
                   numpad_cancel_cb_t cancel,
                   void *user);

#ifdef __cplusplus
}
#endif
