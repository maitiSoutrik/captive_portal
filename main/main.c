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

// HTML files
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

// JavaScript files
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char jquery_js_start[] asm("_binary_jquery_3_3_1_min_js_start");
extern const char jquery_js_end[] asm("_binary_jquery_3_3_1_min_js_end");

// CSS files
extern const char app_css_start[] asm("_binary_app_css_start");
extern const char app_css_end[] asm("_binary_app_css_end");

// Favicon
extern const char favicon_start[] asm("_binary_favicon_ico_start");
extern const char favicon_end[] asm("_binary_favicon_ico_end");

static const char *TAG = "example";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void wifi_init_softap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:'%s' password:'%s'",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

// Index HTML Handler
static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    const uint32_t index_html_len = index_html_end - index_html_start;

    ESP_LOGI(TAG, "Serve index.html");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_len);

    return ESP_OK;
}

// app.js Handler
static esp_err_t app_js_get_handler(httpd_req_t *req)
{
    const uint32_t app_js_len = app_js_end - app_js_start;

    ESP_LOGI(TAG, "Serve app.js");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, app_js_start, app_js_len);

    return ESP_OK;
}

// jQuery Handler
static esp_err_t jquery_js_get_handler(httpd_req_t *req)
{
    const uint32_t jquery_js_len = jquery_js_end - jquery_js_start;

    ESP_LOGI(TAG, "Serve jquery-3.3.1.min.js");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, jquery_js_start, jquery_js_len);

    return ESP_OK;
}

// app.css Handler
static esp_err_t app_css_get_handler(httpd_req_t *req)
{
    const uint32_t app_css_len = app_css_end - app_css_start;

    ESP_LOGI(TAG, "Serve app.css");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, app_css_start, app_css_len);

    return ESP_OK;
}

// Favicon Handler
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    const uint32_t favicon_len = favicon_end - favicon_start;

    ESP_LOGI(TAG, "Serve favicon.ico");
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, favicon_start, favicon_len);

    return ESP_OK;
}

// URI handlers
static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_html_get_handler
};

static const httpd_uri_t app_js = {
    .uri = "/app.js",
    .method = HTTP_GET,
    .handler = app_js_get_handler
};

static const httpd_uri_t jquery_js = {
    .uri = "/jquery-3.3.1.min.js",
    .method = HTTP_GET,
    .handler = jquery_js_get_handler
};

static const httpd_uri_t app_css = {
    .uri = "/app.css",
    .method = HTTP_GET,
    .handler = app_css_get_handler
};

static const httpd_uri_t favicon = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_get_handler
};

// API Endpoint Handlers

// Get AP SSID Handler
static esp_err_t ap_ssid_handler(httpd_req_t *req)
{
    char json_response[100];

    // Create JSON response with the SSID
    snprintf(json_response, sizeof(json_response), "{\"ssid\":\"%s\"}", EXAMPLE_ESP_WIFI_SSID);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));

    return ESP_OK;
}

// Get Sensor Values Handler (dummy values for now)
static esp_err_t sensor_handler(httpd_req_t *req)
{
    char json_response[100];

    // Create JSON response with dummy sensor values
    // In a real application, you would read from actual sensors
    snprintf(json_response, sizeof(json_response), "{\"temp\":25.5,\"humidity\":60.2}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));

    return ESP_OK;
}

// Get Local Time Handler (dummy time for now)
static esp_err_t local_time_handler(httpd_req_t *req)
{
    char json_response[100];

    // Create JSON response with dummy time
    // In a real application, you would get the actual time from SNTP
    snprintf(json_response, sizeof(json_response), "{\"time\":\"12:34:56 PM\"}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));

    return ESP_OK;
}

// Get WiFi Connection Info Handler
static esp_err_t wifi_connect_info_handler(httpd_req_t *req)
{
    char json_response[200];

    // Create JSON response with connection info
    // In a real application, you would get the actual connection info
    snprintf(json_response, sizeof(json_response), "{\"ap\":\"Not Connected\",\"ip\":\"0.0.0.0\",\"netmask\":\"0.0.0.0\",\"gw\":\"0.0.0.0\"}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));

    return ESP_OK;
}

// WiFi Disconnect Handler
static esp_err_t wifi_disconnect_handler(httpd_req_t *req)
{
    char json_response[50];

    // In a real application, you would disconnect from the WiFi
    // For now, just return a success message
    snprintf(json_response, sizeof(json_response), "{\"status\":\"disconnected\"}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));

    return ESP_OK;
}

// WiFi Connect Handler
static esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    char json_response[50];

    // In a real application, you would connect to the WiFi with the provided credentials
    // For now, just return a success message
    snprintf(json_response, sizeof(json_response), "{\"status\":\"connected\"}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));

    return ESP_OK;
}

// API URI structures
static const httpd_uri_t uri_ap_ssid = {
    .uri = "/apSSID",
    .method = HTTP_GET,
    .handler = ap_ssid_handler
};

static const httpd_uri_t uri_sensor = {
    .uri = "/Sensor",
    .method = HTTP_GET,
    .handler = sensor_handler
};

static const httpd_uri_t uri_local_time = {
    .uri = "/localTime",
    .method = HTTP_GET,
    .handler = local_time_handler
};

static const httpd_uri_t uri_wifi_connect_info = {
    .uri = "/wifiConnectInfo",
    .method = HTTP_GET,
    .handler = wifi_connect_info_handler
};

static const httpd_uri_t uri_wifi_disconnect = {
    .uri = "/wifiDisconnect",
    .method = HTTP_DELETE,
    .handler = wifi_disconnect_handler
};

static const httpd_uri_t uri_wifi_connect = {
    .uri = "/wifiConnect",
    .method = HTTP_POST,
    .handler = wifi_connect_handler
};

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

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &app_js);
        httpd_register_uri_handler(server, &jquery_js);
        httpd_register_uri_handler(server, &app_css);
        httpd_register_uri_handler(server, &favicon);

        // Register API endpoints needed by the webpage
        // API handlers for the webpage functionality
        httpd_register_uri_handler(server, &uri_ap_ssid);
        httpd_register_uri_handler(server, &uri_sensor);
        httpd_register_uri_handler(server, &uri_local_time);
        httpd_register_uri_handler(server, &uri_wifi_connect_info);
        httpd_register_uri_handler(server, &uri_wifi_disconnect);
        httpd_register_uri_handler(server, &uri_wifi_connect);

        // Register error handler
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    return server;
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

    ESP_LOGI(TAG, "ESP Test Value: %d", EXAMPLE_TEST_CONFIG);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop needed by the  main app
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS needed by Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize Wi-Fi including netif with default config
    esp_netif_create_default_wifi_ap();

    // Initialise ESP32 in SoftAP mode
    wifi_init_softap();

    // Start the server for the first time
    start_webserver();

    // Start the DNS server that will redirect all queries to the softAP IP
    start_dns_server();
}
