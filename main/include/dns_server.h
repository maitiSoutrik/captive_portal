/* Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "esp_err.h" // Added for esp_err_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set ups and starts a simple DNS server that will respond to all queries
 * with the soft AP's IP address
 *
 * @return esp_err_t ESP_OK on success, ESP_FAIL or other error code on failure.
 */
esp_err_t start_dns_server(void); // Changed return type to esp_err_t


#ifdef __cplusplus
}
#endif
