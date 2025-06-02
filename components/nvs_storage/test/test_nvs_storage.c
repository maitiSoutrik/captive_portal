#include "unity.h"
#include "nvs_storage.h" // Component to test
#include "nvs_flash.h" // For nvs_flash_init/deinit for on-target tests
#include "esp_system.h" // For esp_err_t
#include "esp_log.h" // For logging

static const char* TAG = "test_nvs_storage";

// This setUp function is for on-target testing where real NVS is used.
// For host-based tests, NVS functions must be mocked.
void setUp(void) {
    ESP_LOGI(TAG, "Initializing NVS for test");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        TEST_ESP_OK(nvs_flash_erase()); // Erase NVS if needed
        ret = nvs_flash_init(); // Retry initialization
    }
    TEST_ESP_OK(ret); // Assert that NVS initialization was successful
}

void tearDown(void) {
    ESP_LOGI(TAG, "De-initializing NVS after test");
    // It's good practice to deinit NVS to leave the system clean,
    // especially if multiple test suites run.
    // However, if tests are very frequent, repeated init/deinit can be slow.
    // TEST_ESP_OK(nvs_flash_deinit());
    // For simplicity in this example, we might leave it initialized if other tests use it.
    // Or, ensure each test component manages its NVS state carefully.
    // A common strategy is to erase the specific namespace used by the test.
}

static void test_nvs_save_and_read_string_successfully(void) {
    const char* test_namespace = "test_ns_rw";
    const char* test_key = "string_key";
    const char* test_value_to_save = "Hello Unity from NVS!";
    char read_buffer[100]; // Ensure buffer is large enough

    // Act: Save the string
    esp_err_t save_err = nvs_storage_save_str(test_namespace, test_key, test_value_to_save);
    TEST_ESP_OK(save_err); // Check if save operation was successful

    // Act: Read the string back
    esp_err_t read_err = nvs_storage_read_str(test_namespace, test_key, read_buffer, sizeof(read_buffer));
    TEST_ESP_OK(read_err); // Check if read operation was successful

    // Assert: Verify the read string matches the saved string
    TEST_ASSERT_EQUAL_STRING(test_value_to_save, read_buffer);

    // Cleanup: Optional, erase the key or namespace if desired after test
    // nvs_handle_t my_handle;
    // TEST_ESP_OK(nvs_open(test_namespace, NVS_READWRITE, &my_handle));
    // TEST_ESP_OK(nvs_erase_key(my_handle, test_key));
    // TEST_ESP_OK(nvs_commit(my_handle));
    // nvs_close(my_handle);
}

static void test_nvs_read_non_existent_key_returns_not_found(void) {
    const char* test_namespace = "test_ns_read";
    const char* non_existent_key = "ghost_key";
    char read_buffer[50];

    // Act: Attempt to read a key that should not exist
    esp_err_t read_err = nvs_storage_read_str(test_namespace, non_existent_key, read_buffer, sizeof(read_buffer));
    
    // Assert: Expect ESP_ERR_NVS_NOT_FOUND error
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NVS_NOT_FOUND, read_err);
}

// Main function to run tests for this component (called by test runner)
// The name of this function doesn't strictly matter as ESP-IDF discovers tests via CMake.
// However, it's good practice to group tests.
void run_nvs_storage_tests(void) { 
    UNITY_BEGIN();
    RUN_TEST(test_nvs_save_and_read_string_successfully);
    RUN_TEST(test_nvs_read_non_existent_key_returns_not_found);
    UNITY_END();
}

// For ESP-IDF, you typically don't define app_main in each test file.
// Instead, test cases are registered with CMake and run by the test harness.
// However, when building a specific test app like this, we need an entry point.
void app_main(void) {
   run_nvs_storage_tests();
}
