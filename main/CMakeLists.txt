idf_component_register(
    SRCS
        lwmalloc.c
        main.cpp
    INCLUDE_DIRS "."
    REQUIRES gui sensors settings bsp_extra esp_event audio_alert nvs_flash wifi_manager ntp_sync sd_logger
)

## enable the next line to upload the spiffs content
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)