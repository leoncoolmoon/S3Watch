idf_component_register(
    SRCS "src/wifi_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash json
)
