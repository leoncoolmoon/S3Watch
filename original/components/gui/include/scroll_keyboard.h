#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Apple-TV-style horizontal scroll keyboard.
// Parent should be a full-screen container.
// The keyboard calls on_done(text, user_data) when the user confirms,
// or on_cancel(user_data) when they back out.
typedef void (*scroll_kb_done_cb_t)(const char *text, void *user_data);
typedef void (*scroll_kb_cancel_cb_t)(void *user_data);

void scroll_keyboard_create(lv_obj_t *parent,
                             const char *initial_text,
                             const char *hint,
                             scroll_kb_done_cb_t   on_done,
                             scroll_kb_cancel_cb_t on_cancel,
                             void *user_data);

#ifdef __cplusplus
}
#endif
