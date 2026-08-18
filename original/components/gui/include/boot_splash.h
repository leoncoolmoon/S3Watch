#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Boot splash — shown by boot_manager between bsp_display_start() and the
// watchface handoff. Boot-only: wake-from-sleep never goes through this.

// Create + load the splash screen ("S3Watch" wordmark, spinner, firmware
// version) and render it synchronously so it's visible immediately. Call once,
// after bsp_display_start(), before the tileview exists.
void boot_splash_show(void);

// Slide the (already-built) main tileview up over the splash
// (LV_SCR_LOAD_ANIM_OVER_TOP, 300 ms) and delete the splash after the
// animation lands. Call once, after ui_task has built the main screen.
void boot_splash_handoff(void);

#ifdef __cplusplus
}
#endif
