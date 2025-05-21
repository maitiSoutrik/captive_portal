#include <stdio.h>
#include "nvs_storage.h"
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h> // For strlen, strcmp

// Define a namespace for Wi-Fi credentials
#define WIFI_NAMESPACE "wifi_config"
// Update NVS keys to match the source identifiers for clarity
#define KEY_SSID "my-connect-ssid"
#define KEY_PASSWORD "my-connect-pswd"

void nvs_storage_init(void)
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_storage_test();
}

bool nvs_storage_test(void)
{
    // Open
    printf("\n");
    printf("Opening Non-Volatile Storage (NVS) handle for restart_counter... ");
    nvs_handle_t my_handle;
    // Using "nvs" namespace for the restart_counter example, as in the original code.
    esp_err_t err = nvs_open("nvs", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle ('nvs') for restart_counter!\n", esp_err_to_name(err));
        return false; // Indicate test failure
    } else {
        printf("Done\n");

        // Read
        printf("Reading restart counter from NVS ... ");
        int32_t restart_counter = 0; // value will default to 0, if not set yet in NVS
        err = nvs_get_i32(my_handle, "restart_counter", &restart_counter);
        switch (err) {
            case ESP_OK:
                printf("Done\n");
                printf("Restart counter = %" PRIu32 "\n", restart_counter);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                printf("The value 'restart_counter' is not initialized yet!\n");
                break;
            default :
                printf("Error (%s) reading 'restart_counter'!\n", esp_err_to_name(err));
        }

        // Write
        printf("Updating restart counter in NVS ... ");
        restart_counter++;
        err = nvs_set_i32(my_handle, "restart_counter", restart_counter);
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

        // Commit written value.
        printf("Committing updates for 'restart_counter' in NVS ... ");
        err = nvs_commit(my_handle);
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

        // Close
        nvs_close(my_handle);
    }

    return (err == ESP_OK); // Return true if the last operation (commit) was successful
}

esp_err_t nvs_storage_save_wifi_creds(const char *ssid, const char *password) {
    nvs_handle_t my_handle;
    esp_err_t err;

    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Open NVS
    err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle for saving Wi-Fi creds ('%s')!\n", esp_err_to_name(err), WIFI_NAMESPACE);
        return err;
    }

    // Write SSID
    err = nvs_set_str(my_handle, KEY_SSID, ssid);
    if (err != ESP_OK) {
        printf("Error (%s) writing SSID ('%s') to NVS!\n", esp_err_to_name(err), KEY_SSID);
        nvs_close(my_handle);
        return err;
    }

    // Write Password
    err = nvs_set_str(my_handle, KEY_PASSWORD, password);
    if (err != ESP_OK) {
        printf("Error (%s) writing Password ('%s') to NVS!\n", esp_err_to_name(err), KEY_PASSWORD);
        nvs_close(my_handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) committing Wi-Fi creds to NVS!\n", esp_err_to_name(err));
    } else {
        printf("Wi-Fi credentials saved successfully to NVS namespace '%s'.\n", WIFI_NAMESPACE);
    }

    // Close NVS
    nvs_close(my_handle);
    return err;
}

esp_err_t nvs_storage_load_wifi_creds(char *ssid_buf, size_t ssid_buf_size, char *pass_buf, size_t pass_buf_size) {
    nvs_handle_t my_handle;
    esp_err_t err;

    if (ssid_buf == NULL || pass_buf == NULL || ssid_buf_size == 0 || pass_buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Open NVS
    err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
             printf("NVS namespace '%s' not found. Wi-Fi credentials likely not saved yet.\n", WIFI_NAMESPACE);
        } else {
            printf("Error (%s) opening NVS handle for loading Wi-Fi creds ('%s')!\n", esp_err_to_name(err), WIFI_NAMESPACE);
        }
        return err;
    }

    // Read SSID
    size_t required_size_ssid = ssid_buf_size;
    err = nvs_get_str(my_handle, KEY_SSID, ssid_buf, &required_size_ssid);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            printf("SSID ('%s') not found in NVS namespace '%s'.\n", KEY_SSID, WIFI_NAMESPACE);
        } else if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            printf("Error reading SSID ('%s'): Buffer too small. Required size: %zu, Buffer size: %zu.\n", KEY_SSID, required_size_ssid, ssid_buf_size);
        }
        else {
            printf("Error (%s) reading SSID ('%s') from NVS!\n", esp_err_to_name(err), KEY_SSID);
        }
        nvs_close(my_handle);
        return err;
    }
    // Ensure null termination if nvs_get_str succeeded and buffer might be full
    // nvs_get_str includes null terminator if space allows. If required_size_ssid == ssid_buf_size,
    // it means the string (including terminator) fit perfectly or was truncated to fit *without* terminator.
    if (required_size_ssid == ssid_buf_size && ssid_buf_size > 0) {
        ssid_buf[ssid_buf_size - 1] = '\0';
    }


    // Read Password
    size_t required_size_pass = pass_buf_size;
    err = nvs_get_str(my_handle, KEY_PASSWORD, pass_buf, &required_size_pass);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            printf("Password ('%s') not found in NVS namespace '%s'.\n", KEY_PASSWORD, WIFI_NAMESPACE);
        } else if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            printf("Error reading Password ('%s'): Buffer too small. Required size: %zu, Buffer size: %zu.\n", KEY_PASSWORD, required_size_pass, pass_buf_size);
        } else {
            printf("Error (%s) reading Password ('%s') from NVS!\n", esp_err_to_name(err), KEY_PASSWORD);
        }
        nvs_close(my_handle);
        return err;
    }
    if (required_size_pass == pass_buf_size && pass_buf_size > 0) {
         pass_buf[pass_buf_size - 1] = '\0';
    }

    printf("Wi-Fi credentials loaded successfully from NVS namespace '%s'.\n", WIFI_NAMESPACE);
    nvs_close(my_handle);
    return ESP_OK;
}

// Optional: Test function for Wi-Fi credentials
bool nvs_storage_test_wifi_creds(void) {
    const char *test_ssid = "test_nvs_ssid";
    const char *test_pass = "test_nvs_password";
    char retrieved_ssid[64]; // Ensure buffer is large enough
    char retrieved_pass[64]; // Ensure buffer is large enough
    esp_err_t err;

    printf("\nTesting Wi-Fi credential NVS storage...\n");

    // Save
    printf("Attempting to save test Wi-Fi creds: SSID='%s', PASS='%s'\n", test_ssid, test_pass);
    err = nvs_storage_save_wifi_creds(test_ssid, test_pass);
    if (err != ESP_OK) {
        printf("Failed to save test Wi-Fi creds: %s\n", esp_err_to_name(err));
        return false;
    }
    printf("Test Wi-Fi creds saved.\n");

    // Load
    printf("Attempting to load test Wi-Fi creds...\n");
    err = nvs_storage_load_wifi_creds(retrieved_ssid, sizeof(retrieved_ssid), retrieved_pass, sizeof(retrieved_pass));
    if (err != ESP_OK) {
        printf("Failed to load test Wi-Fi creds: %s\n", esp_err_to_name(err));
        return false;
    }
    printf("Test Wi-Fi creds loaded: SSID='%s', PASS='%s'\n", retrieved_ssid, retrieved_pass);

    // Verify
    if (strcmp(test_ssid, retrieved_ssid) == 0 && strcmp(test_pass, retrieved_pass) == 0) {
        printf("SUCCESS: Wi-Fi credential NVS test PASSED!\n");
        return true;
    } else {
        printf("FAILURE: Wi-Fi credential NVS test FAILED! Mismatch.\n");
        printf("Expected SSID: '%s', Got: '%s'\n", test_ssid, retrieved_ssid);
        printf("Expected Pass: '%s', Got: '%s'\n", test_pass, retrieved_pass);
        return false;
    }
}
