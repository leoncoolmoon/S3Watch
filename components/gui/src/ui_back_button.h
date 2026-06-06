// Internal header: hardware back-button + PMU short-press → "Back" action.

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Wire up the GPIO 0 back-button (ISR + task) and register the PMU short-press
// coordinator subscriber. Call once from ui_task() after display_manager_init.
void ui_back_button_start(void);

#ifdef __cplusplus
}
#endif
