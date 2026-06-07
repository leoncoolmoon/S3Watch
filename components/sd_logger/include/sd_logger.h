#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors all ESP_LOG output to a file under /sdcard/logs/ for the lifetime
// of this boot session, so crashes that only happen away from a serial
// monitor can still be diagnosed afterwards.
//
// Gated by settings_get_sd_logging_enabled() — does nothing (no card access,
// no hook installed) when the setting is off. Call once, early in app_main(),
// before other subsystems start logging heavily.
void sd_logger_init(void);

#ifdef __cplusplus
}
#endif
