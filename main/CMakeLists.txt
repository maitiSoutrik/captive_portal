idf_component_register(
    SRCS "main.c" "dns_server.c"
    INCLUDE_DIRS "include"
    REQUIRES app_wifi app_time_sync app_local_server nvs_storage rfid_manager spi_ffs_storage esp_netif esp_event unity rgb_led
)
