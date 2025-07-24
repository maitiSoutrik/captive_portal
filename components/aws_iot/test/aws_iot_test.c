#include <stdio.h>
#include "unity.h"
#include "aws_iot.h"
#include "app_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// Event group to signal when Wi-Fi is connected
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// Callback function to handle WiFi events, as required by app_wifi_init
static void test_wifi_event_callback(app_wifi_status_t status, void *user_data)
{
    if (status == APP_WIFI_STATUS_CONNECTED) {
        printf("TEST_CALLBACK: Wi-Fi Connected.\n");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Helper function to initialize all prerequisites and connect to Wi-Fi
static void connect_to_wifi_for_test(void)
{
    static bool initialized = false;
    if (initialized) {
        if((xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) == WIFI_CONNECTED_BIT) {
            return;
        }
    }

    printf("TEST_SETUP: Initializing prerequisites...\n");
    
    wifi_event_group = xEventGroupCreate();
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP adapter and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize the app_wifi component in STA mode
    ESP_ERROR_CHECK(app_wifi_init(APP_WIFI_MODE_STA, test_wifi_event_callback, NULL));

    // Start the Wi-Fi. This will use credentials stored in NVS.
    ESP_ERROR_CHECK(app_wifi_start());

    printf("TEST_SETUP: Waiting for Wi-Fi connection (timeout: 20 seconds)...\n");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));

    TEST_ASSERT_TRUE_MESSAGE(bits & WIFI_CONNECTED_BIT, "Failed to connect to Wi-Fi for the test.");
    
    initialized = true;
    printf("TEST_SETUP: Wi-Fi connection successful.\n");
}

TEST_CASE("aws_iot_start can be called after wifi connection", "[aws_iot]")
{
    // 1. Setup
    connect_to_wifi_for_test();

    // 2. Execute
    printf("TEST_EXEC: Calling aws_iot_start()...\n");
    aws_iot_start();

    // 3. Assert
    // The main assertion is that the call to aws_iot_start() does not crash.
    // The task created by aws_iot_start will likely fail internally because
    // certificates are not embedded in the test build, but the call itself should succeed.
    printf("TEST_ASSERT: aws_iot_start() was called without crashing.\n");
    
    // We can add a small delay to allow the new task to start and potentially print an error.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
