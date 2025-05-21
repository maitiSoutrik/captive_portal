#ifndef __NVS_STORAGE_H__
#define __NVS_STORAGE_H__

#include <stdbool.h>
#include "esp_err.h"
#include <stddef.h>

/**
 * @brief Initialize NVS storage
 */
void nvs_storage_init(void);

/**
 * @brief Test NVS storage
 * @return true if test passed, false otherwise
 */
bool nvs_storage_test(void);

/**
 * @brief Save Wi-Fi credentials (SSID and password) to NVS.
 *
 * @param ssid The SSID of the Wi-Fi network.
 * @param password The password of the Wi-Fi network.
 * @return esp_err_t ESP_OK on success, or an error code from NVS functions.
 */
esp_err_t nvs_storage_save_wifi_creds(const char *ssid, const char *password);

/**
 * @brief Load Wi-Fi credentials (SSID and password) from NVS.
 *
 * @param ssid_buf Buffer to store the loaded SSID.
 * @param ssid_buf_size Size of the SSID buffer.
 * @param pass_buf Buffer to store the loaded password.
 * @param pass_buf_size Size of the password buffer.
 * @return esp_err_t ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if credentials are not found,
 *         or another error code from NVS functions.
 */
esp_err_t nvs_storage_load_wifi_creds(char *ssid_buf, size_t ssid_buf_size, char *pass_buf, size_t pass_buf_size);

#endif