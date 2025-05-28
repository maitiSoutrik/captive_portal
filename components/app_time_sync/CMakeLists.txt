idf_component_register(
    SRCS "app_time_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif lwip esp_event
)
