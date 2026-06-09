idf_component_register(
    SRCS
        "src/music_catalog.c"
        "src/music_player.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES esp32_s3_touch_amoled_2_06 settings audio_manager json sd_manager
)
