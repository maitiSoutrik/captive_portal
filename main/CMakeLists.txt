idf_component_register(SRCS
                        "main.c" 
                        "dns_server.c"
                        "time_sync.c"
                        "app_station.c" 
                        INCLUDE_DIRS "include"
                        REQUIRES app_local_server nvs_flash esp_wifi)
