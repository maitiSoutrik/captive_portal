/* Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

// Component includes
#include "app_wifi.h"
#include "app_time_sync.h"
#include "app_local_server.h"
#include "dns_server.h"
#include "nvs_storage.h"
#include "spi_ffs_storage.h"
#include "rfid_manager.h" // Added for RFID Management
#include "custom_partition.h" // Added for custom NVS partition testing

#define EXAMPLE_ESP_WIFI_AP_SSID CONFIG_ESP_WIFI_AP_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_AP_PASSWORD
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_AP_STA_CONN

static const char *TAG = "main";

/* Event callback functions */
static void wifi_event_callback(app_wifi_status_t status, void *user_data);
static void time_sync_callback(app_time_sync_status_t status, time_t current_time, void *user_data);

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Captive Portal Application");

    /* Initialize core systems */
    ESP_LOGI(TAG, "Initializing NVS storage");
    nvs_storage_init(); // nvs_storage_init returns void
    
    // ESP_LOGI(TAG, "Testing NVS custom partition");
    // nvs_custom_partition_test(); // Test the custom NVS functions

    ESP_LOGI(TAG, "Initializing network interface");
    ESP_ERROR_CHECK(esp_netif_init());
    
    ESP_LOGI(TAG, "Creating default event loop");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    /* Initialize storage systems */
    ESP_LOGI(TAG, "Initializing SPI FFS storage");
    ESP_ERROR_CHECK(spi_ffs_storage_init());

    ESP_LOGI(TAG, "Initializing RFID Manager");
    ESP_ERROR_CHECK(rfid_manager_init());
    
    /* Test storage functionality */
    // ESP_LOGI(TAG, "Testing SPI FFS storage");
    // spi_ffs_storage_test();
    // spi_ffs_storage_test_all_functions();
    
    /* Initialize and configure WiFi */
    ESP_LOGI(TAG, "Initializing WiFi component");
    ESP_ERROR_CHECK(app_wifi_init(APP_WIFI_MODE_APSTA, wifi_event_callback, NULL));
    // app_wifi_register_event_callback was removed, callback passed to init
    
    /* Configure Access Point */
    ESP_LOGI(TAG, "Configuring WiFi Access Point");
    ESP_ERROR_CHECK(app_wifi_set_ap_config(EXAMPLE_ESP_WIFI_AP_SSID, 
                                           EXAMPLE_ESP_WIFI_PASS, 
                                           EXAMPLE_MAX_STA_CONN));
    
    /* Start WiFi */
    ESP_LOGI(TAG, "Starting WiFi");
    ESP_ERROR_CHECK(app_wifi_start());
    
    /* Initialize time synchronization */
    ESP_LOGI(TAG, "Initializing time synchronization");
    ESP_ERROR_CHECK(app_time_sync_init(time_sync_callback, NULL));
    // app_time_sync_register_callback was removed, callback passed to init
    
    /* Start time synchronization */
    ESP_LOGI(TAG, "Starting time synchronization");
    ESP_ERROR_CHECK(app_time_sync_start());
    
    /* Initialize and start HTTP server */
    ESP_LOGI(TAG, "Initializing HTTP server");
    ESP_ERROR_CHECK(app_local_server_init());
    ESP_ERROR_CHECK(app_local_server_start());
    
    /* Start DNS server */
    ESP_LOGI(TAG, "Starting DNS server");
    ESP_ERROR_CHECK(start_dns_server());
    
    /* Log startup completion */
    // AP IP logging removed as app_wifi_get_ap_ip was removed. 
    // If needed, obtain from esp_netif directly or add a simplified getter.
    ESP_LOGI(TAG, "Captive portal started successfully!");
    ESP_LOGI(TAG, "SSID: %s", EXAMPLE_ESP_WIFI_AP_SSID);
    if (strlen(EXAMPLE_ESP_WIFI_PASS) > 0) {
        ESP_LOGI(TAG, "Password: %s", EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGI(TAG, "Open network (no password)");
    }
    
    /* Main application loop */
    ESP_LOGI(TAG, "Entering main application loop");
    while (1) {
        /* Process HTTP server requests */
        // app_local_server_process now returns esp_err_t, but its return is not critical for the loop to continue.
        // If an error occurs within, it's logged by the component.
        // If it were critical, we would do: ESP_ERROR_CHECK_WITHOUT_ABORT(app_local_server_process()); or handle it.
        app_local_server_process();

        rfid_manager_process();
        
        /* Small delay to prevent watchdog timeout */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief WiFi event callback handler
 * 
 * This function is called whenever there's a change in WiFi status
 * (connection, disconnection, etc.)
 */
static void wifi_event_callback(app_wifi_status_t status, void *user_data)
{
    switch (status) {
        case APP_WIFI_STATUS_CONNECTED:
            {
                ESP_LOGI(TAG, "WiFi station connected successfully");
                // STA IP is logged by app_wifi component on IP_EVENT_STA_GOT_IP.
                // Number of connected stations logging removed as app_wifi_get_connected_stations was removed.
            }
            break;
            
        case APP_WIFI_STATUS_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi station disconnected");
            break;
            
        case APP_WIFI_STATUS_CONNECTING:
            ESP_LOGI(TAG, "WiFi station connecting...");
            break;
            
        case APP_WIFI_STATUS_FAILED:
            ESP_LOGW(TAG, "WiFi station connection failed");
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown WiFi status: %u", status);
            break;
    }
}

/**
 * @brief Time synchronization callback handler
 * 
 * This function is called whenever there's a change in time sync status
 */
static void time_sync_callback(app_time_sync_status_t status, time_t current_time, void *user_data)
{
    switch (status) {
        case APP_TIME_SYNC_STATUS_COMPLETED:
            {
                ESP_LOGI(TAG, "Time synchronization completed successfully");
                // Time formatting, DST, and offset logging removed as related functions were removed.
                // current_time (time_t) is available if needed.
                struct tm timeinfo;
                char strftime_buf[64];
                localtime_r(&current_time, &timeinfo);
                strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
                ESP_LOGI(TAG, "Current local time: %s", strftime_buf);
            }
            break;
            
        case APP_TIME_SYNC_STATUS_IN_PROGRESS:
            ESP_LOGI(TAG, "Time synchronization in progress...");
            break;
            
        case APP_TIME_SYNC_STATUS_FAILED:
            ESP_LOGW(TAG, "Time synchronization failed");
            break;
            
        case APP_TIME_SYNC_STATUS_NOT_STARTED:
            ESP_LOGI(TAG, "Time synchronization not started");
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown time sync status: %u", status);
            break;
    }
}
