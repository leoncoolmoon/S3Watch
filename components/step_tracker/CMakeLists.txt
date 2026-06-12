idf_component_register(
    SRCS "step_tracker.c"
    INCLUDE_DIRS "include"
    REQUIRES imu_manager settings nvs_flash bsp_extra
)
