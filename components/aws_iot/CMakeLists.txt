idf_component_register(SRCS "aws_iot.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos dht22_sensor esp_system fatfs driver nvs_flash app_wifi esp-aws-iot)

target_add_binary_data(${COMPONENT_TARGET} "certs/aws_root_ca_pem" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/certificate_pem_crt" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/private_pem_key" TEXT)
