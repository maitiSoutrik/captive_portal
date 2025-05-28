/**
 * @file app_wifi.h
 * @brief WiFi management component for ESP32 captive portal
 * 
 * This component provides a clean API for managing WiFi operations including
 * both Access Point (AP) and Station (STA) modes. It encapsulates ESP-IDF
 * WiFi APIs and provides event callbacks for status updates.
 */

#ifndef APP_WIFI_H
#define APP_WIFI_H

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi operation modes
 */
typedef enum {
    APP_WIFI_MODE_AP,     /**< Access Point mode only */
    APP_WIFI_MODE_STA,    /**< Station mode only */
    APP_WIFI_MODE_APSTA   /**< Both AP and STA modes */
} app_wifi_mode_t;

/**
 * @brief WiFi connection status
 */
typedef enum {
    APP_WIFI_STATUS_DISCONNECTED,  /**< Not connected to any network */
    APP_WIFI_STATUS_CONNECTING,    /**< Connection in progress */
    APP_WIFI_STATUS_CONNECTED,     /**< Successfully connected */
    APP_WIFI_STATUS_FAILED         /**< Connection failed */
} app_wifi_status_t;

/**
 * @brief WiFi configuration structure
 * 
 * Note: This structure is primarily for internal use within the component
 * to store the last configured settings.
 */
typedef struct {
    char ap_ssid[32];              /**< AP SSID */
    char ap_password[64];          /**< AP password */
    uint8_t ap_max_connections;    /**< Maximum AP connections */
    char sta_ssid[32];             /**< Station SSID */
    char sta_password[64];         /**< Station password */
    uint8_t sta_max_retry;         /**< Maximum connection retries */
} app_wifi_config_t;

/**
 * @brief WiFi event callback function type
 * 
 * @param status Current WiFi status
 * @param user_data User provided data
 */
typedef void (*app_wifi_event_cb_t)(app_wifi_status_t status, void *user_data);

/**
 * @brief Initialize WiFi component
 * 
 * @param mode WiFi operation mode
 * @param event_callback Callback function for WiFi status updates
 * @param user_data User data to pass to the event callback
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_init(app_wifi_mode_t mode, app_wifi_event_cb_t event_callback, void *user_data);

/**
 * @brief Start WiFi operations
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_start(void);

/**
 * @brief Stop WiFi operations
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_stop(void);

/**
 * @brief Deinitialize WiFi component
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_deinit(void);

/**
 * @brief Configure Access Point settings
 * 
 * @param ssid AP SSID (max 31 characters)
 * @param password AP password (8-63 characters, or empty for open)
 * @param max_conn Maximum number of connections (1-10)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_set_ap_config(const char *ssid, const char *password, uint8_t max_conn);

/**
 * @brief Configure Station settings
 * 
 * @param ssid Network SSID to connect to
 * @param password Network password
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_set_sta_config(const char *ssid, const char *password);

/**
 * @brief Connect to a WiFi network in station mode
 * 
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief Disconnect from current WiFi network
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_wifi_disconnect_sta(void);

#ifdef __cplusplus
}
#endif

#endif // APP_WIFI_H
