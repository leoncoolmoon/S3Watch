#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_alert_init(void);
void audio_alert_notify(void);
void audio_alert_play_startup(void);
void audio_alert_suspend(void);

#ifdef __cplusplus
}
#endif
