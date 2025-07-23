/**
 * @file app_wifi.c
 * @brief WiFi management component implementation
 * 
 * This component handles all WiFi operations for the ESP32 captive portal,
 * including AP and STA mode management, event handling, and connection management.
 */

#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_storage.h"
#include "nvs_flash.h" // Added for NVS error codes
#include "app_wifi.h"
#include "aws_iot.h"

/* Constants */
#define TAG "app_wifi"
#define DEFAULT_SCAN_LIST_SIZE 20
#define MAX_RETRY_DEFAULT 5

/* WiFi authentication mode threshold */
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

/* WPA3 SAE mode configuration */
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

/* Private types */
typedef struct {
    bool initialized;
    bool started;
    app_wifi_mode_t mode;
    app_wifi_config_t config;
    app_wifi_status_t sta_status;
    esp_netif_t *ap_netif;
    esp_netif_t *sta_netif;
    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    int retry_count;
    SemaphoreHandle_t mutex;
    app_wifi_event_cb_t event_callback; // Moved to end
    void *event_callback_arg; // Moved to end
} app_wifi_ctx_t;

/* Private variables */
static app_wifi_ctx_t s_wifi_ctx = {
    .initialized = false,
    .started = false,
    .sta_status = APP_WIFI_STATUS_DISCONNECTED,
    .retry_count = 0,
    .mutex = NULL,
    .event_callback = NULL, // Initialize
    .event_callback_arg = NULL // Initialize
};

/* Private function prototypes */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void notify_status_change(app_wifi_status_t new_status);
static esp_err_t validate_ap_config(const char *ssid, const char *password, uint8_t max_conn);
static esp_err_t validate_sta_config(const char *ssid, const char *password);

/* Public API Implementation */

esp_err_t app_wifi_init(app_wifi_mode_t mode, app_wifi_event_cb_t event_callback, void *user_data)
{
    if (s_wifi_ctx.initialized) {
        ESP_LOGW(TAG, "WiFi already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WiFi in mode %d", mode);

    /* Create mutex for thread safety */
    s_wifi_ctx.mutex = xSemaphoreCreateMutex();
    if (s_wifi_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialize netif based on mode */
    if (mode == APP_WIFI_MODE_AP || mode == APP_WIFI_MODE_APSTA) {
        s_wifi_ctx.ap_netif = esp_netif_create_default_wifi_ap();
        if (s_wifi_ctx.ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create AP netif");
            vSemaphoreDelete(s_wifi_ctx.mutex);
            return ESP_FAIL;
        }
    }

    if (mode == APP_WIFI_MODE_STA || mode == APP_WIFI_MODE_APSTA) {
        s_wifi_ctx.sta_netif = esp_netif_create_default_wifi_sta();
        if (s_wifi_ctx.sta_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create STA netif");
            if (s_wifi_ctx.ap_netif) {
                esp_netif_destroy(s_wifi_ctx.ap_netif);
            }
            vSemaphoreDelete(s_wifi_ctx.mutex);
            return ESP_FAIL;
        }
    }

    /* Initialize WiFi with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        if (s_wifi_ctx.ap_netif) esp_netif_destroy(s_wifi_ctx.ap_netif);
        if (s_wifi_ctx.sta_netif) esp_netif_destroy(s_wifi_ctx.sta_netif);
        vSemaphoreDelete(s_wifi_ctx.mutex);
        return ret;
    }

    /* Register event handlers */
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                             ESP_EVENT_ANY_ID,
                                             &wifi_event_handler,
                                             NULL,
                                             &s_wifi_ctx.wifi_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        if (s_wifi_ctx.ap_netif) esp_netif_destroy(s_wifi_ctx.ap_netif);
        if (s_wifi_ctx.sta_netif) esp_netif_destroy(s_wifi_ctx.sta_netif);
        vSemaphoreDelete(s_wifi_ctx.mutex);
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                             IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler,
                                             NULL,
                                             &s_wifi_ctx.ip_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_ctx.wifi_event_instance);
        esp_wifi_deinit();
        if (s_wifi_ctx.ap_netif) esp_netif_destroy(s_wifi_ctx.ap_netif);
        if (s_wifi_ctx.sta_netif) esp_netif_destroy(s_wifi_ctx.sta_netif);
        vSemaphoreDelete(s_wifi_ctx.mutex);
        return ret;
    }

    /* Set WiFi mode */
    wifi_mode_t wifi_mode;
    switch (mode) {
        case APP_WIFI_MODE_AP:
            wifi_mode = WIFI_MODE_AP;
            break;
        case APP_WIFI_MODE_STA:
            wifi_mode = WIFI_MODE_STA;
            break;
        case APP_WIFI_MODE_APSTA:
            wifi_mode = WIFI_MODE_APSTA;
            break;
        default:
            ESP_LOGE(TAG, "Invalid WiFi mode");
            return ESP_ERR_INVALID_ARG;
    }

    ret = esp_wifi_set_mode(wifi_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_ctx.wifi_event_instance);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_wifi_ctx.ip_event_instance);
        esp_wifi_deinit();
        if (s_wifi_ctx.ap_netif) esp_netif_destroy(s_wifi_ctx.ap_netif);
        if (s_wifi_ctx.sta_netif) esp_netif_destroy(s_wifi_ctx.sta_netif);
        vSemaphoreDelete(s_wifi_ctx.mutex);
        return ret;
    }

    /* Set default configurations */
    s_wifi_ctx.mode = mode;
    s_wifi_ctx.config.sta_max_retry = MAX_RETRY_DEFAULT;
    s_wifi_ctx.event_callback = event_callback;
    s_wifi_ctx.event_callback_arg = user_data;
    s_wifi_ctx.initialized = true;

    ESP_LOGI(TAG, "WiFi initialized successfully");
    return ESP_OK;
}

esp_err_t app_wifi_start(void)
{
    if (!s_wifi_ctx.initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_ctx.started) {
        ESP_LOGW(TAG, "WiFi already started");
        return ESP_OK;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    /* Load WiFi credentials from NVS for station mode */
    if (s_wifi_ctx.mode == APP_WIFI_MODE_STA || s_wifi_ctx.mode == APP_WIFI_MODE_APSTA) {
        char nvs_ssid[32] = {0};
        char nvs_password[64] = {0};
        
        esp_err_t err = nvs_storage_load_wifi_creds(nvs_ssid, sizeof(nvs_ssid), 
                                                    nvs_password, sizeof(nvs_password));
        
        if (err == ESP_OK && strlen(nvs_ssid) > 0) {
            ESP_LOGI(TAG, "Found WiFi credentials in NVS. SSID: %s", nvs_ssid);
            strncpy(s_wifi_ctx.config.sta_ssid, nvs_ssid, sizeof(s_wifi_ctx.config.sta_ssid) - 1);
            strncpy(s_wifi_ctx.config.sta_password, nvs_password, sizeof(s_wifi_ctx.config.sta_password) - 1);
            
            /* Configure station */
            wifi_config_t wifi_config = {0};
            strncpy((char *)wifi_config.sta.ssid, s_wifi_ctx.config.sta_ssid, sizeof(wifi_config.sta.ssid) - 1);
            strncpy((char *)wifi_config.sta.password, s_wifi_ctx.config.sta_password, sizeof(wifi_config.sta.password) - 1);
            wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
            wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;
            
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        } else {
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGI(TAG, "No WiFi credentials found in NVS");
            } else {
                ESP_LOGW(TAG, "Error loading WiFi credentials from NVS: %s", esp_err_to_name(err));
            }
        }
    }

    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ret;
    }

    s_wifi_ctx.started = true;
    xSemaphoreGive(s_wifi_ctx.mutex);

    ESP_LOGI(TAG, "WiFi started successfully");
    return ESP_OK;
}

esp_err_t app_wifi_stop(void)
{
    if (!s_wifi_ctx.initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_wifi_ctx.started) {
        ESP_LOGW(TAG, "WiFi not started");
        return ESP_OK;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ret;
    }

    s_wifi_ctx.started = false;
    s_wifi_ctx.sta_status = APP_WIFI_STATUS_DISCONNECTED;
    xSemaphoreGive(s_wifi_ctx.mutex);

    ESP_LOGI(TAG, "WiFi stopped successfully");
    return ESP_OK;
}

esp_err_t app_wifi_deinit(void)
{
    if (!s_wifi_ctx.initialized) {
        ESP_LOGW(TAG, "WiFi not initialized");
        return ESP_OK;
    }

    if (s_wifi_ctx.started) {
        esp_err_t ret = app_wifi_stop();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    /* Unregister event handlers */
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_ctx.wifi_event_instance);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_wifi_ctx.ip_event_instance);

    /* Deinitialize WiFi */
    esp_err_t ret = esp_wifi_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinit WiFi: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ret;
    }

    /* Destroy netif */
    if (s_wifi_ctx.ap_netif) {
        esp_netif_destroy(s_wifi_ctx.ap_netif);
        s_wifi_ctx.ap_netif = NULL;
    }
    if (s_wifi_ctx.sta_netif) {
        esp_netif_destroy(s_wifi_ctx.sta_netif);
        s_wifi_ctx.sta_netif = NULL;
    }

    s_wifi_ctx.initialized = false;
    xSemaphoreGive(s_wifi_ctx.mutex);
    
    vSemaphoreDelete(s_wifi_ctx.mutex);
    s_wifi_ctx.mutex = NULL;

    ESP_LOGI(TAG, "WiFi deinitialized successfully");
    return ESP_OK;
}

esp_err_t app_wifi_set_ap_config(const char *ssid, const char *password, uint8_t max_conn)
{
    if (!s_wifi_ctx.initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_ctx.mode != APP_WIFI_MODE_AP && s_wifi_ctx.mode != APP_WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "AP mode not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = validate_ap_config(ssid, password, max_conn);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    /* Store configuration */
    strncpy(s_wifi_ctx.config.ap_ssid, ssid, sizeof(s_wifi_ctx.config.ap_ssid) - 1);
    strncpy(s_wifi_ctx.config.ap_password, password, sizeof(s_wifi_ctx.config.ap_password) - 1);
    s_wifi_ctx.config.ap_max_connections = max_conn;

    /* Apply configuration */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ssid),
            .max_connection = max_conn,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    
    if (strlen(password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_wifi_ctx.mutex);

    ESP_LOGI(TAG, "AP configured: SSID='%s', max_conn=%d", ssid, max_conn);
    return ret;
}

esp_err_t app_wifi_set_sta_config(const char *ssid, const char *password)
{
    if (!s_wifi_ctx.initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_ctx.mode != APP_WIFI_MODE_STA && s_wifi_ctx.mode != APP_WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Station mode not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = validate_sta_config(ssid, password);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    /* Store configuration */
    strncpy(s_wifi_ctx.config.sta_ssid, ssid, sizeof(s_wifi_ctx.config.sta_ssid) - 1);
    strncpy(s_wifi_ctx.config.sta_password, password, sizeof(s_wifi_ctx.config.sta_password) - 1);

    /* Save to NVS */
    ret = nvs_storage_save_wifi_creds(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save WiFi credentials to NVS: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_wifi_ctx.mutex);

    ESP_LOGI(TAG, "Station configured: SSID='%s'", ssid);
    return ESP_OK;
}


esp_err_t app_wifi_connect_sta(const char *ssid, const char *password)
{
    if (!s_wifi_ctx.initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_ctx.mode != APP_WIFI_MODE_STA && s_wifi_ctx.mode != APP_WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Station mode not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_wifi_ctx.started) {
        ESP_LOGE(TAG, "WiFi not started");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = validate_sta_config(ssid, password);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    ESP_LOGI(TAG, "Attempting to connect to SSID: %s", ssid);

    /* Reset retry counter */
    s_wifi_ctx.retry_count = 0;

    /* Update configuration */
    strncpy(s_wifi_ctx.config.sta_ssid, ssid, sizeof(s_wifi_ctx.config.sta_ssid) - 1);
    strncpy(s_wifi_ctx.config.sta_password, password, sizeof(s_wifi_ctx.config.sta_password) - 1);

    /* Configure WiFi */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
    wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;

    /* Disconnect if already connected */
    esp_wifi_disconnect();

    /* Set new configuration */
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set station config: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ret;
    }

    /* Attempt to connect */
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ret;
    }

    /* Update status */
    s_wifi_ctx.sta_status = APP_WIFI_STATUS_CONNECTING;
    notify_status_change(APP_WIFI_STATUS_CONNECTING);

    /* Save credentials to NVS */
    nvs_storage_save_wifi_creds(ssid, password);

    xSemaphoreGive(s_wifi_ctx.mutex);

    ESP_LOGI(TAG, "Connection initiated to %s", ssid);
    return ESP_OK;
}

esp_err_t app_wifi_disconnect_sta(void)
{
    if (!s_wifi_ctx.initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_ctx.mode != APP_WIFI_MODE_STA && s_wifi_ctx.mode != APP_WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Station mode not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ret;
    }

    s_wifi_ctx.sta_status = APP_WIFI_STATUS_DISCONNECTED;
    s_wifi_ctx.retry_count = s_wifi_ctx.config.sta_max_retry; // Prevent auto-reconnect
    notify_status_change(APP_WIFI_STATUS_DISCONNECTED);

    xSemaphoreGive(s_wifi_ctx.mutex);

    ESP_LOGI(TAG, "Disconnected from WiFi");
    return ESP_OK;
}

esp_err_t app_wifi_get_current_sta_ssid(char *ssid_buffer, size_t buffer_len)
{
    if (!s_wifi_ctx.initialized || !s_wifi_ctx.started) {
        ESP_LOGW(TAG, "WiFi not initialized or not started, cannot get STA SSID.");
        if (buffer_len > 0) ssid_buffer[0] = '\0';
        return ESP_ERR_WIFI_NOT_STARTED;
    }

    if (ssid_buffer == NULL || buffer_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Ensure buffer is initially empty
    ssid_buffer[0] = '\0'; 

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    esp_err_t ret = ESP_FAIL; // Default to failure

    if (s_wifi_ctx.sta_status == APP_WIFI_STATUS_CONNECTED) {
        wifi_config_t wifi_config;
        ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

        if (ret == ESP_OK) {
            if (strlen((char *)wifi_config.sta.ssid) > 0) {
                strncpy(ssid_buffer, (char *)wifi_config.sta.ssid, buffer_len - 1);
                ssid_buffer[buffer_len - 1] = '\0'; // Ensure null termination
                ESP_LOGI(TAG, "Current STA SSID from esp_wifi_get_config: %s", ssid_buffer);
            } else {
                ESP_LOGW(TAG, "esp_wifi_get_config STA SSID is empty, though status is connected.");
                // Consider if ESP_ERR_WIFI_SSID_NOT_FOUND is defined or if ESP_FAIL is better
                ret = ESP_FAIL; // Using ESP_FAIL as a generic failure for empty SSID
            }
        } else {
            ESP_LOGE(TAG, "Failed to get STA config via esp_wifi_get_config: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "Not currently connected to a station. STA status: %d", s_wifi_ctx.sta_status);
        ret = ESP_ERR_WIFI_NOT_CONNECT; // Indicate not connected
    }

    xSemaphoreGive(s_wifi_ctx.mutex);
    return ret;
}
/* Private function implementations */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
                {
                    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                    ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
                }
                break;
                
            case WIFI_EVENT_AP_STADISCONNECTED:
                {
                    wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                    ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
                }
                break;
                
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Station started");
                if (strlen(s_wifi_ctx.config.sta_ssid) > 0) {
                    esp_wifi_connect();
                }
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                {
                    wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                    ESP_LOGI(TAG, "Disconnected from AP, reason: %d", event->reason);
                    
                    if (s_wifi_ctx.retry_count < s_wifi_ctx.config.sta_max_retry) {
                        esp_wifi_connect();
                        s_wifi_ctx.retry_count++;
                        ESP_LOGI(TAG, "Retrying connection (%d/%d)...", 
                                s_wifi_ctx.retry_count, s_wifi_ctx.config.sta_max_retry);
                        notify_status_change(APP_WIFI_STATUS_CONNECTING);
                    } else {
                        ESP_LOGI(TAG, "Failed to connect after %d retries", s_wifi_ctx.config.sta_max_retry);
                        s_wifi_ctx.sta_status = APP_WIFI_STATUS_FAILED;
                        notify_status_change(APP_WIFI_STATUS_FAILED);
                    }
                }
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                aws_iot_start(); // Start AWS IoT after successful connection
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_ctx.retry_count = 0;
        s_wifi_ctx.sta_status = APP_WIFI_STATUS_CONNECTED;
        notify_status_change(APP_WIFI_STATUS_CONNECTED);
    }
}

static void notify_status_change(app_wifi_status_t new_status)
{
    if (s_wifi_ctx.event_callback) {
        s_wifi_ctx.event_callback(new_status, s_wifi_ctx.event_callback_arg);
    }
}

static esp_err_t validate_ap_config(const char *ssid, const char *password, uint8_t max_conn)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(ssid) > 31) {
        ESP_LOGE(TAG, "SSID too long (max 31 characters)");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (password != NULL && strlen(password) > 0 && strlen(password) < 8) {
        ESP_LOGE(TAG, "Password too short (min 8 characters)");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (password != NULL && strlen(password) > 63) {
        ESP_LOGE(TAG, "Password too long (max 63 characters)");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (max_conn < 1 || max_conn > 10) {
        ESP_LOGE(TAG, "Invalid max connections (1-10)");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

static esp_err_t validate_sta_config(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(ssid) > 31) {
        ESP_LOGE(TAG, "SSID too long (max 31 characters)");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (password == NULL) {
        ESP_LOGE(TAG, "Password cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(password) > 63) {
        ESP_LOGE(TAG, "Password too long (max 63 characters)");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}
