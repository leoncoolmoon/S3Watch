idf_component_register(
    SRCS
        "src/music_catalog.c"
        "src/music_player.c"
        "third_party/minimp3/minimp3_impl.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "third_party/minimp3"
    PRIV_REQUIRES esp32_s3_touch_amoled_2_06 settings audio_alert power_manager json
)
