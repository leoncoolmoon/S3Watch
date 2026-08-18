idf_component_register(
    SRCS "boot_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES gui audio_alert audio_manager alarm_manager power_manager settings
             sd_manager sd_logger wifi_manager ntp_sync signalk_client
             imu_manager step_tracker bsp_extra esp32_s3_touch_amoled_2_06
             nvs_flash json esp_event esp_timer
)
