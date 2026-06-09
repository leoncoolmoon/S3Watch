# On-device test runner: a small registry of pure-software and hardware
# tests invoked from Settings → Run Tests. Each test is one C function;
# the runner iterates them sequentially and dispatches progress via a
# user-provided callback.
#
# The three shared software suites compile the same source files used by
# the native host runner under tests/ — single source of truth.

set(SHARED_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tests")

idf_component_register(
    SRCS
        "src/ondev_test.c"

        # Software suites
        "src/sw_suite_utc_tm.c"
        "src/sw_suite_ip_parse.c"
        "src/sw_suite_signalk_alert_parse.c"
        "src/sw_suite_signalk_delta.c"
        "src/sw_suite_signalk_alert_cache.c"
        "src/sw_suite_dashboard_format.c"
        "src/sw_suite_settings.c"

        # Hardware tests
        "src/hw_test_heap.c"
        "src/hw_test_i2c_scan.c"
        "src/hw_test_axp.c"
        "src/hw_test_rtc.c"
        "src/hw_test_nvs.c"
        "src/hw_test_spiffs.c"
        "src/hw_test_imu.c"
        "src/hw_test_audio.c"
        "src/hw_test_brightness.c"
        "src/hw_test_wifi_scan.c"
        "src/hw_test_coord_stats.c"
        "src/hw_test_sd.c"

        # Reused host test bodies, compiled here for the firmware build
        "${SHARED_TESTS_DIR}/test_utc_tm.c"
        "${SHARED_TESTS_DIR}/test_ip_parse.c"
        "${SHARED_TESTS_DIR}/test_signalk_alert.c"

    INCLUDE_DIRS
        "include"

    PRIV_INCLUDE_DIRS
        "${SHARED_TESTS_DIR}"

    REQUIRES
        bsp_extra
        esp32_s3_touch_amoled_2_06
        task_coordinator
        wifi_manager
        audio_alert
        signalk_client
        settings
        sensors
        gui
        sd_manager

    PRIV_REQUIRES
        nvs_flash
        spiffs
        esp_event
        esp_wifi
        esp_timer
)
