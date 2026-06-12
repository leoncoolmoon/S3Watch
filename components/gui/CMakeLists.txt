set(SRCS "")

set(SRC_DIRS "")
list(APPEND SRC_DIRS "font")
list(APPEND SRC_DIRS "icons")
list(APPEND SRC_DIRS "src")

set(INCLUDE_DIRS "")
list(APPEND INCLUDE_DIRS "include")

foreach(SRC_DIR IN LISTS SRC_DIRS)
    file(GLOB_RECURSE SRC ${SRC_DIR}/*.c)
    list(APPEND SRCS ${SRC})    
endforeach()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES lvgl imu_manager step_tracker settings display_manager power_manager esp32_s3_touch_amoled_2_06 audio_alert alarm_manager bsp_extra wifi_manager ntp_sync task_coordinator signalk_client ondev_test music_player esp_app_format
    PRIV_REQUIRES esp_event
)
