idf_component_register(SRCS "custom_partition.c" "nvs_storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash)
