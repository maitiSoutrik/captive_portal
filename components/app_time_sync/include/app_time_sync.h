/**
 * @file app_time_sync.h
 * @brief Time synchronization component for ESP32
 * 
 * This component provides time synchronization functionality using SNTP,
 * timezone management, and time formatting utilities.
 */

#ifndef APP_TIME_SYNC_H
#define APP_TIME_SYNC_H

#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Time synchronization status
 */
typedef enum {
    APP_TIME_SYNC_STATUS_NOT_STARTED,   /**< Time sync not started */
    APP_TIME_SYNC_STATUS_IN_PROGRESS,   /**< Time sync in progress */
    APP_TIME_SYNC_STATUS_COMPLETED,     /**< Time sync completed successfully */
    APP_TIME_SYNC_STATUS_FAILED         /**< Time sync failed */
} app_time_sync_status_t;

/**
 * @brief Time synchronization configuration
 * 
 * Note: This structure is primarily for internal use within the component
 * to store the last configured settings.
 */
typedef struct {
    char ntp_server[64];        /**< NTP server address */
    char timezone[32];          /**< Timezone string (e.g., "CST6CDT,M3.2.0,M11.1.0") */
    uint32_t sync_timeout_ms;   /**< Sync timeout in milliseconds */
    uint32_t retry_count;       /**< Number of retry attempts */
} app_time_sync_config_t;

/**
 * @brief Time sync callback function type
 * 
 * @param status Current sync status
 * @param current_time Current time (valid only if status is COMPLETED)
 * @param user_data User provided data
 */
typedef void (*app_time_sync_cb_t)(app_time_sync_status_t status, time_t current_time, void *user_data);

/**
 * @brief Initialize time synchronization component
 * 
 * @param callback Callback function for time sync status updates
 * @param user_data User data to pass to the callback
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_time_sync_init(app_time_sync_cb_t callback, void *user_data);

/**
 * @brief Start time synchronization
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_time_sync_start(void);

/**
 * @brief Stop time synchronization
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_time_sync_stop(void);

/**
 * @brief Deinitialize time synchronization component
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_time_sync_deinit(void);

/**
 * @brief Set timezone
 * 
 * @param timezone Timezone string (e.g., "CST6CDT,M3.2.0,M11.1.0" for Chicago)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_time_sync_set_timezone(const char *timezone);

/**
 * @brief Set NTP server
 * 
 * @param server NTP server address
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_time_sync_set_ntp_server(const char *server);

#ifdef __cplusplus
}
#endif

#endif // APP_TIME_SYNC_H
