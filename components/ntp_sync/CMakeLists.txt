idf_component_register(
    SRCS "src/ntp_sync.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_netif lwip wifi_manager settings bsp_extra esp_event esp_timer task_coordinator
)
