idf_component_register(
    SRCS
        main.cpp
    INCLUDE_DIRS "."
    REQUIRES gui sensors settings bsp_extra power_manager audio_manager esp_event audio_alert nvs_flash wifi_manager ntp_sync sd_logger
)

## enable the next line to upload the spiffs content
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)