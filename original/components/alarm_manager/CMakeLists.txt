idf_component_register(
    SRCS
        "src/alarm_manager.c"
    INCLUDE_DIRS
        "include"
    REQUIRES settings audio_alert display_manager power_manager task_coordinator
)
