#include "custom_partition.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h" // For ESP_OK, ESP_FAIL etc.
#include <string.h>

static const char *TAG = "custom_nvs";
static const char *NVS_PARTITION_LABEL = "params"; // Using the "params" partition as requested

// Helper function to open NVS handle for the custom partition
static esp_err_t open_nvs_handle(nvs_handle_t *out_handle) {
    if (out_handle == NULL) {
        ESP_LOGE(TAG, "out_handle parameter cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize NVS for the "params" partition
    // Note: nvs_flash_init_partition is idempotent, so calling it multiple times for the same partition is safe.
    // However, the main nvs_storage_init() should have initialized the default NVS partition.
    // For a *custom* named partition, we must ensure it's initialized.
    esp_err_t err = nvs_flash_init_partition(NVS_PARTITION_LABEL);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition '%s' init failed (%s), attempting to erase and reinitialize...", NVS_PARTITION_LABEL, esp_err_to_name(err));
        // Attempt to erase the partition and re-initialize
        esp_err_t erase_err = nvs_flash_erase_partition(NVS_PARTITION_LABEL);
        if (erase_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS partition '%s': %s", NVS_PARTITION_LABEL, esp_err_to_name(erase_err));
            return erase_err; // Return erase error
        }
        err = nvs_flash_init_partition(NVS_PARTITION_LABEL);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS partition '%s' after potential erase/reinit: %s", NVS_PARTITION_LABEL, esp_err_to_name(err));
        return err; // Return initialization error
    }

    // Open NVS
    err = nvs_open_from_partition(NVS_PARTITION_LABEL, "storage_ns", NVS_READWRITE, out_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle for partition '%s' and namespace 'storage_ns': %s", NVS_PARTITION_LABEL, esp_err_to_name(err));
    }
    return err;
}

bool nvs_custom_partition_set_params(const char *key, const char *value) {
    if (key == NULL || value == NULL) {
        ESP_LOGE(TAG, "Set params: Key or value is NULL.");
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = open_nvs_handle(&nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_set_str(nvs_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set string for key '%s' in partition '%s': %s", key, NVS_PARTITION_LABEL, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes for partition '%s': %s", NVS_PARTITION_LABEL, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Successfully set key '%s' in NVS partition '%s'", key, NVS_PARTITION_LABEL);
    return true;
}

bool nvs_custom_partition_get_params(const char *key, char *value, size_t value_len) {
    if (key == NULL || value == NULL || value_len == 0) {
        ESP_LOGE(TAG, "Get params: Key, value buffer, or value_len is invalid.");
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = open_nvs_handle(&nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, key, NULL, &required_size); // Get required size first
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Key '%s' not found in NVS partition '%s'", key, NVS_PARTITION_LABEL);
        nvs_close(nvs_handle);
        return false;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get required size for key '%s' in partition '%s': %s", key, NVS_PARTITION_LABEL, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    if (required_size > value_len) {
        ESP_LOGE(TAG, "Buffer too small for key '%s'. Required: %d, Available: %d", key, required_size, value_len);
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_get_str(nvs_handle, key, value, &value_len); // Actual read
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get string for key '%s' in partition '%s': %s", key, NVS_PARTITION_LABEL, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Successfully got key '%s' from NVS partition '%s', value: '%s'", key, NVS_PARTITION_LABEL, value);
    return true;
}

void nvs_custom_partition_test(void) {
    ESP_LOGI(TAG, "--- Starting NVS Custom Partition Test ('%s') ---", NVS_PARTITION_LABEL);

    const char *test_key = "test_param_key";
    const char *test_value_set = "HelloCustomNVS!";
    char test_value_get[50]; // Buffer to retrieve the value

    // Test Set
    ESP_LOGI(TAG, "Attempting to set key: '%s', value: '%s'", test_key, test_value_set);
    if (nvs_custom_partition_set_params(test_key, test_value_set)) {
        ESP_LOGI(TAG, "Set operation successful.");
    } else {
        ESP_LOGE(TAG, "Set operation FAILED.");
        // No point in continuing if set failed
        ESP_LOGI(TAG, "--- NVS Custom Partition Test ('%s') Finished (with errors) ---", NVS_PARTITION_LABEL);
        return;
    }

    // Test Get
    ESP_LOGI(TAG, "Attempting to get key: '%s'", test_key);
    memset(test_value_get, 0, sizeof(test_value_get)); // Clear buffer before get
    if (nvs_custom_partition_get_params(test_key, test_value_get, sizeof(test_value_get))) {
        ESP_LOGI(TAG, "Get operation successful. Retrieved value: '%s'", test_value_get);
        if (strcmp(test_value_set, test_value_get) == 0) {
            ESP_LOGI(TAG, "SUCCESS: Set and Get values match!");
        } else {
            ESP_LOGE(TAG, "FAILURE: Set ('%s') and Get ('%s') values DO NOT match!", test_value_set, test_value_get);
        }
    } else {
        ESP_LOGE(TAG, "Get operation FAILED.");
    }

    // Test Get non-existent key
    const char *non_existent_key = "no_such_key";
    ESP_LOGI(TAG, "Attempting to get non-existent key: '%s'", non_existent_key);
    if (nvs_custom_partition_get_params(non_existent_key, test_value_get, sizeof(test_value_get))) {
        ESP_LOGE(TAG, "FAILURE: Get operation unexpectedly succeeded for non-existent key '%s'. Value: '%s'", non_existent_key, test_value_get);
    } else {
        ESP_LOGI(TAG, "SUCCESS: Get operation correctly failed for non-existent key '%s' (as expected).", non_existent_key);
    }
    
    ESP_LOGI(TAG, "--- NVS Custom Partition Test ('%s') Finished ---", NVS_PARTITION_LABEL);
}
