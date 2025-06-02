idf_component_register(SRCS "nvs_storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash)

if(IDF_BUILD_TESTS)
    # Test registration
    set(TEST_SRCS "test/test_nvs_storage.c")

    # Register the unit test.
    idf_component_add_unit_test(
        SRCS ${TEST_SRCS}
        # For nvs_storage tests, we need 'unity' for the test framework,
        # 'nvs_flash' because the tests perform nvs_flash_init/erase,
        # 'esp_system' for esp_err_t, and 'esp_log' for logging.
        # 'nvs_storage' (the component itself) is implicitly included.
        COMPONENT_REQUIRES unity nvs_flash esp_system esp_log
    )
endif()
