/* Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/inet.h"

#include "esp_http_server.h"
#include "dns_server.h"

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN
#define EXAMPLE_TEST_CONFIG CONFIG_ESP_TEST_VALUE

extern const char jquery_3_3_1_min_js_start[] asm("_binary_jquery_3_3_1_min_js_start");
extern const char jquery_3_3_1_min_js_end[] asm("_binary_jquery_3_3_1_min_js_end");
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char app_css_start[] asm("_binary_app_css_start");
extern const char app_css_end[] asm("_binary_app_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const char favicon_ico_end[] asm("_binary_favicon_ico_end");

static const char *TAG = "example";

httpd_handle_t http_server_handle = NULL;

static void handler_initialize(void);
static esp_err_t http_server_j_query_handler(httpd_req_t *req);
static esp_err_t http_server_index_html_handler(httpd_req_t *req);
static esp_err_t http_server_app_css_handler(httpd_req_t *req);
static esp_err_t http_server_app_js_handler(httpd_req_t *req);
static esp_err_t http_server_favicon_handler(httpd_req_t *req);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void wifi_init_softap(void)
{

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

        ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&http_server_handle, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        handler_initialize();
        httpd_register_err_handler(http_server_handle, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
}

void app_main(void)
{
        /*
        Turn of warnings from HTTP server as redirecting traffic will yield
        lots of invalid requests
    */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    ESP_LOGI(TAG,"ESP TEST VALUE %d",CONFIG_ESP_TEST_VALUE);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop needed by the  main app
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS needed by Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initi Wi-Fi including netif with default config
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));


    // Initialise ESP32 in SoftAP mode
    wifi_init_softap();

    start_webserver();

    // Start the DNS server that will redirect all queries to the softAP IP
    start_dns_server();

    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:'%s' password:'%s'",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);   
    while(1)
    {
//       esp_task_wdt_reset();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    } 
}

static void handler_initialize(void)
{
    // Register jQuery handler
    httpd_uri_t jquery_js =
        {
            .uri = "/jquery-3.3.1.min.js",
            .method = HTTP_GET,
            .handler = http_server_j_query_handler,
            .user_ctx = NULL};
    // Register index.html handler
    httpd_uri_t index_html =
        {
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_server_index_html_handler,
            .user_ctx = NULL};
    // Register app.css handler
    httpd_uri_t app_css =
        {
            .uri = "/app.css",
            .method = HTTP_GET,
            .handler = http_server_app_css_handler,
            .user_ctx = NULL};
    // Register app.js handler
    httpd_uri_t app_js =
        {
            .uri = "/app.js",
            .method = HTTP_GET,
            .handler = http_server_app_js_handler,
            .user_ctx = NULL};
    // Register favicon.ico handler
    httpd_uri_t favicon_ico =
        {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = http_server_favicon_handler,
            .user_ctx = NULL};

    httpd_register_uri_handler(http_server_handle, &jquery_js);
    httpd_register_uri_handler(http_server_handle, &index_html);
    httpd_register_uri_handler(http_server_handle, &app_css);
    httpd_register_uri_handler(http_server_handle, &app_js);
    httpd_register_uri_handler(http_server_handle, &favicon_ico);
}

/*
 * jQuery get handler requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_j_query_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "JQuery Requested");
    httpd_resp_set_type(req, "application/javascript");
    error = httpd_resp_send(req, (const char *)jquery_3_3_1_min_js_start, jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start);
    if (error != ESP_OK)
    {
        ESP_LOGI(TAG, "http_server_j_query_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_j_query_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * Send the index HTML page
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "Index HTML Requested");
    httpd_resp_set_type(req, "text/html");
    error = httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    if (error != ESP_OK)
    {
        ESP_LOGI(TAG, "http_server_index_html_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_index_html_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * app.css get handler is requested when accessing the web page
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "APP CSS Requested");
    httpd_resp_set_type(req, "text/css");
    error = httpd_resp_send(req, (const char *)app_css_start, app_css_end - app_css_start);
    if (error != ESP_OK)
    {
        ESP_LOGI(TAG, "http_server_app_css_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_app_css_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * app.js get handler requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "APP JS Requested");
    httpd_resp_set_type(req, "application/javascript");
    error = httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
    if (error != ESP_OK)
    {
        ESP_LOGI(TAG, "http_server_app_js_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_app_js_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * Sends the .ico file when accessing the web page
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_favicon_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "Favicon.ico Requested");
    httpd_resp_set_type(req, "image/x-icon");
    error = httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);
    if (error != ESP_OK)
    {
        ESP_LOGI(TAG, "http_server_favicon_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_favicon_handler: Response Sent Successfully");
    }
    return error;
}
