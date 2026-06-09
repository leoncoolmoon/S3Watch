idf_component_register(
    SRCS
        "src/audio_alert.c"
    INCLUDE_DIRS
        "include"
    REQUIRES audio_manager settings
)
