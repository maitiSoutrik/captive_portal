/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_storage.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#define TAG "wifi station"

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif



static int s_retry_num = 0;

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        // esp_wifi_connect() will be called by wifi_init_sta or app_station_connect_to_ap
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START: System will attempt to connect.");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        // wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        // ESP_LOGI(TAG, "Disconnect reason: %d", event->reason);
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect(); // Attempt to reconnect with the current config
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying to connect to the AP (%d/%d)...", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY);
        }
        else
        {
            ESP_LOGI(TAG, "Failed to connect to the AP after %d retries.", EXAMPLE_ESP_MAXIMUM_RETRY);
            // Optionally, signal permanent failure here
        }
        // http_server_monitor_send_msg(HTTP_MSG_WIFI_CONNECT_FAIL); // Example for future status update
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0; // Reset retry count on successful connection
        // http_server_monitor_send_msg(HTTP_MSG_WIFI_CONNECT_SUCCESS); // Example for future status update
    }
}

esp_err_t app_station_connect_to_ap(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Attempting to connect to SSID: %s", ssid);

    s_retry_num = 0; // Reset retry counter for new connection attempt

    wifi_config_t wifi_config = {0}; // Initialize to zero
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD; // Or determine dynamically
    wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE; // If using WPA3
    // wifi_config.sta.sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER; // If using WPA3

    // Disconnect if already connected or attempting
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    // Set the new configuration
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // Attempt to connect
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "esp_wifi_connect() called successfully for %s.", ssid);
    } else {
        ESP_LOGE(TAG, "esp_wifi_connect() failed for %s: %s", ssid, esp_err_to_name(err));
    }
    return err;
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {0}; // Initialize to zero
    char nvs_ssid[32] = {0};
    char nvs_password[64] = {0};

    esp_err_t err = nvs_storage_load_wifi_creds(nvs_ssid, sizeof(nvs_ssid), nvs_password, sizeof(nvs_password));

    if (err == ESP_OK && strlen(nvs_ssid) > 0)
    {
        ESP_LOGI(TAG, "Found credentials in NVS. SSID: %s", nvs_ssid);
        strncpy((char *)wifi_config.sta.ssid, nvs_ssid, sizeof(wifi_config.sta.ssid) -1);
        strncpy((char *)wifi_config.sta.password, nvs_password, sizeof(wifi_config.sta.password) -1);
    }
    else
    {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No Wi-Fi credentials found in NVS. Using Kconfig default.");
        } else {
            ESP_LOGW(TAG, "Error loading Wi-Fi credentials from NVS (%s). Using Kconfig default.", esp_err_to_name(err));
        }
        // Fallback to Kconfig defaults if NVS fails or no creds found
        strncpy((char *)wifi_config.sta.ssid, EXAMPLE_ESP_WIFI_SSID, sizeof(wifi_config.sta.ssid)-1);
        strncpy((char *)wifi_config.sta.password, EXAMPLE_ESP_WIFI_PASS, sizeof(wifi_config.sta.password)-1);
    }

    wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
    wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;
    // wifi_config.sta.sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "wifi_init_sta finished. Will attempt to connect to %s.", wifi_config.sta.ssid);
    // esp_wifi_connect() will be called by WIFI_EVENT_STA_START event if Wi-Fi is started
    // Or, if Wi-Fi is already started, we might need to call it explicitly here if we want an immediate attempt on boot.
    // For now, the WIFI_EVENT_STA_START handler in main.c will trigger the first connect.
}