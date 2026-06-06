idf_component_register(
    SRCS "src/signalk_client.c"
    INCLUDE_DIRS "include"
    REQUIRES json esp_event
    PRIV_REQUIRES esp_websocket_client esp_wifi wifi_manager settings display_manager
)
