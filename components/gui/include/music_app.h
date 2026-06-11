#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

void music_app_create(lv_obj_t *parent);

// Jump the (already-created) music app to its Now-Playing screen. Used by the
// display-wake hook so an actively-playing track comes up on Now Playing for
// one-tap pause/stop. No-op if the screen isn't mounted.
void music_app_goto_now_playing(void);

#ifdef __cplusplus
}
#endif
