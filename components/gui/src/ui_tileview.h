// Internal header: tileview construction and dynamic-tile lifecycle.
// The ui_dynamic_* public API itself is declared in ui.h.

#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

// Create the main tileview (watchface + controls) and register it with the
// screen manager. Called once during ui_init().
void swatch_tileview(void);

// Back-button action: pop one level of tileview depth. Closes a dynamic
// subtile if open, else closes the dynamic tile, else navigates to the
// watchface tile. Must be called on the LVGL thread.
void ui_tileview_back(void);

// Snap the tileview back to the watchface (no animation), discarding any
// open dynamic settings tiles. Called from the display-wake pre-show hook
// so every wake comes up at home rather than on whatever tile the user
// was last viewing before the screen slept.
void ui_tileview_reset_to_watchface(void);

#ifdef __cplusplus
}
#endif
