/**
 * @file app_time_sync.c
 * @brief Time synchronization component implementation
 * 
 * This component handles SNTP-based time synchronization, timezone management,
 * and time formatting utilities for the ESP32.
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "lwip/ip_addr.h"
#include <inttypes.h> // For PRIu32
#include "app_time_sync.h"

/* Constants */
#define TAG "app_time_sync"
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE "CST6CDT,M3.2.0,M11.1.0"  // Chicago timezone
#define DEFAULT_SYNC_TIMEOUT_MS 30000
#define DEFAULT_RETRY_COUNT 15
#define SYNC_CHECK_INTERVAL_MS 2000
#define MIN_VALID_YEAR 2020

/* Private types */
typedef struct {
    bool initialized;
    bool started;
    app_time_sync_status_t status;
    app_time_sync_config_t config;
    SemaphoreHandle_t mutex;
    TaskHandle_t sync_task_handle;
    app_time_sync_cb_t callback;
    void *callback_arg;
} app_time_sync_ctx_t;

/* Private variables */
static app_time_sync_ctx_t s_time_sync_ctx = {
    .initialized = false,
    .started = false,
    .status = APP_TIME_SYNC_STATUS_NOT_STARTED,
    .mutex = NULL,
    .sync_task_handle = NULL,
    .callback = NULL,
    .callback_arg = NULL
};

/* Private function prototypes */
static void time_sync_notification_cb(struct timeval *tv);
static void time_sync_task(void *pvParameters);
static bool is_time_valid(void);
static void notify_status_change(app_time_sync_status_t new_status, time_t current_time);

/* Public API Implementation */

esp_err_t app_time_sync_init(app_time_sync_cb_t callback, void *user_data)
{
    if (s_time_sync_ctx.initialized) {
        ESP_LOGW(TAG, "Time sync already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing time synchronization");

    s_time_sync_ctx.mutex = xSemaphoreCreateMutex();
    if (s_time_sync_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_time_sync_ctx.config.ntp_server, DEFAULT_NTP_SERVER, 
            sizeof(s_time_sync_ctx.config.ntp_server) - 1);
    strncpy(s_time_sync_ctx.config.timezone, DEFAULT_TIMEZONE, 
            sizeof(s_time_sync_ctx.config.timezone) - 1);
    s_time_sync_ctx.config.sync_timeout_ms = DEFAULT_SYNC_TIMEOUT_MS;
    s_time_sync_ctx.config.retry_count = DEFAULT_RETRY_COUNT;

    setenv("TZ", s_time_sync_ctx.config.timezone, 1);
    tzset();

    s_time_sync_ctx.callback = callback;
    s_time_sync_ctx.callback_arg = user_data;
    s_time_sync_ctx.initialized = true;
    s_time_sync_ctx.status = APP_TIME_SYNC_STATUS_NOT_STARTED;

    ESP_LOGI(TAG, "Time sync initialized successfully");
    return ESP_OK;
}

esp_err_t app_time_sync_start(void)
{
    if (!s_time_sync_ctx.initialized) {
        ESP_LOGE(TAG, "Time sync not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_time_sync_ctx.started) {
        ESP_LOGW(TAG, "Time sync already started");
        return ESP_OK;
    }

    xSemaphoreTake(s_time_sync_ctx.mutex, portMAX_DELAY);

    if (is_time_valid()) {
        ESP_LOGI(TAG, "System time is already valid");
        s_time_sync_ctx.status = APP_TIME_SYNC_STATUS_COMPLETED;
        s_time_sync_ctx.started = true;
        notify_status_change(APP_TIME_SYNC_STATUS_COMPLETED, time(NULL));
        xSemaphoreGive(s_time_sync_ctx.mutex);
        return ESP_OK;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_time_sync_ctx.config.ntp_server);
    config.sync_cb = time_sync_notification_cb;
    config.renew_servers_after_new_IP = true;
    
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_time_sync_ctx.mutex);
        return ret;
    }

    BaseType_t xReturned = xTaskCreate(time_sync_task, "time_sync_task", 
                                       4096, NULL, 5, &s_time_sync_ctx.sync_task_handle);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create time sync task");
        esp_netif_sntp_deinit();
        xSemaphoreGive(s_time_sync_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    s_time_sync_ctx.started = true;
    s_time_sync_ctx.status = APP_TIME_SYNC_STATUS_IN_PROGRESS;
    notify_status_change(APP_TIME_SYNC_STATUS_IN_PROGRESS, 0);

    xSemaphoreGive(s_time_sync_ctx.mutex);

    ESP_LOGI(TAG, "Time sync started successfully");
    return ESP_OK;
}

esp_err_t app_time_sync_stop(void)
{
    if (!s_time_sync_ctx.initialized) {
        ESP_LOGE(TAG, "Time sync not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_time_sync_ctx.started) {
        ESP_LOGW(TAG, "Time sync not started");
        return ESP_OK;
    }

    xSemaphoreTake(s_time_sync_ctx.mutex, portMAX_DELAY);

    if (s_time_sync_ctx.sync_task_handle != NULL) {
        vTaskDelete(s_time_sync_ctx.sync_task_handle);
        s_time_sync_ctx.sync_task_handle = NULL;
    }

    esp_netif_sntp_deinit();

    s_time_sync_ctx.started = false;
    s_time_sync_ctx.status = APP_TIME_SYNC_STATUS_NOT_STARTED;

    xSemaphoreGive(s_time_sync_ctx.mutex);

    ESP_LOGI(TAG, "Time sync stopped successfully");
    return ESP_OK;
}

esp_err_t app_time_sync_deinit(void)
{
    if (!s_time_sync_ctx.initialized) {
        ESP_LOGW(TAG, "Time sync not initialized");
        return ESP_OK;
    }

    if (s_time_sync_ctx.started) {
        esp_err_t ret = app_time_sync_stop();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    xSemaphoreTake(s_time_sync_ctx.mutex, portMAX_DELAY);
    s_time_sync_ctx.initialized = false;
    xSemaphoreGive(s_time_sync_ctx.mutex);

    vSemaphoreDelete(s_time_sync_ctx.mutex);
    s_time_sync_ctx.mutex = NULL;

    ESP_LOGI(TAG, "Time sync deinitialized successfully");
    return ESP_OK;
}

esp_err_t app_time_sync_set_timezone(const char *timezone)
{
    if (!s_time_sync_ctx.initialized) {
        ESP_LOGE(TAG, "Time sync not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (timezone == NULL || strlen(timezone) == 0) {
        ESP_LOGE(TAG, "Invalid timezone");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(timezone) >= sizeof(s_time_sync_ctx.config.timezone)) {
        ESP_LOGE(TAG, "Timezone string too long");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_time_sync_ctx.mutex, portMAX_DELAY);

    strncpy(s_time_sync_ctx.config.timezone, timezone, 
            sizeof(s_time_sync_ctx.config.timezone) - 1);
    
    setenv("TZ", timezone, 1);
    tzset();

    xSemaphoreGive(s_time_sync_ctx.mutex);

    ESP_LOGI(TAG, "Timezone set to: %s", timezone);
    return ESP_OK;
}

esp_err_t app_time_sync_set_ntp_server(const char *server)
{
    if (!s_time_sync_ctx.initialized) {
        ESP_LOGE(TAG, "Time sync not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (server == NULL || strlen(server) == 0) {
        ESP_LOGE(TAG, "Invalid NTP server");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(server) >= sizeof(s_time_sync_ctx.config.ntp_server)) {
        ESP_LOGE(TAG, "NTP server string too long");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_time_sync_ctx.mutex, portMAX_DELAY);

    strncpy(s_time_sync_ctx.config.ntp_server, server, 
            sizeof(s_time_sync_ctx.config.ntp_server) - 1);

    if (s_time_sync_ctx.started) {
        esp_sntp_setservername(0, server);
    }

    xSemaphoreGive(s_time_sync_ctx.mutex);

    ESP_LOGI(TAG, "NTP server set to: %s", server);
    return ESP_OK;
}

/* Private function implementations */

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronization event received");
    
    if (s_time_sync_ctx.status != APP_TIME_SYNC_STATUS_COMPLETED) {
        s_time_sync_ctx.status = APP_TIME_SYNC_STATUS_COMPLETED;
        notify_status_change(APP_TIME_SYNC_STATUS_COMPLETED, tv->tv_sec);
    }
}

static void time_sync_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Time sync task started");
    
    uint32_t retry_count = 0;
    uint32_t elapsed_ms = 0;
    
    while (retry_count < s_time_sync_ctx.config.retry_count && 
           elapsed_ms < s_time_sync_ctx.config.sync_timeout_ms) {
        
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SYNC_CHECK_INTERVAL_MS)) != ESP_ERR_TIMEOUT) {
            time_t now = time(NULL);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            
            char strftime_buf[64];
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(TAG, "Time synchronized: %s", strftime_buf);
            
            s_time_sync_ctx.status = APP_TIME_SYNC_STATUS_COMPLETED;
            notify_status_change(APP_TIME_SYNC_STATUS_COMPLETED, now);
            
            s_time_sync_ctx.sync_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        }
        
        retry_count++;
        elapsed_ms += SYNC_CHECK_INTERVAL_MS;
        ESP_LOGI(TAG, "Waiting for time sync... (%" PRIu32 "/%" PRIu32 ", %" PRIu32 " ms elapsed)", 
                retry_count, s_time_sync_ctx.config.retry_count, elapsed_ms);
    }
    
    ESP_LOGE(TAG, "Time synchronization failed after %" PRIu32 " retries", retry_count);
    s_time_sync_ctx.status = APP_TIME_SYNC_STATUS_FAILED;
    notify_status_change(APP_TIME_SYNC_STATUS_FAILED, 0);
    
    s_time_sync_ctx.sync_task_handle = NULL;
    vTaskDelete(NULL);
}

static bool is_time_valid(void)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    return (timeinfo.tm_year >= (MIN_VALID_YEAR - 1900));
}

static void notify_status_change(app_time_sync_status_t new_status, time_t current_time)
{
    if (s_time_sync_ctx.callback) {
        s_time_sync_ctx.callback(new_status, current_time, s_time_sync_ctx.callback_arg);
    }
}
