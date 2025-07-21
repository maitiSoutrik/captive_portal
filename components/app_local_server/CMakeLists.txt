idf_component_register(SRCS "app_local_server.c"
                    INCLUDE_DIRS "include"
                    REQUIRES json esp_http_server esp_timer esp_wifi app_update nvs_storage dht22_sensor
                    EMBED_FILES webpage/index.html webpage/app.css webpage/app.js webpage/jquery-3.3.1.min.js webpage/favicon.ico webpage/rfid_management.html webpage/rfid_management.js
                    PRIV_REQUIRES main nvs_storage)
