idf_component_register(
    SRCS "src/sd_logger.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp32_s3_touch_amoled_2_06 settings sd_manager
)
