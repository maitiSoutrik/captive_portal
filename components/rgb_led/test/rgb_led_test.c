#include "unity.h"
#include "rgb_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

TEST_CASE("rgb_led_wifi_app_started_test", "[rgb_led]")
{
    printf("Testing WiFi App Started color...\n");
    rgb_led_wifi_app_started();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to observe color
    TEST_PASS();
}

TEST_CASE("rgb_led_http_server_started_test", "[rgb_led]")
{
    printf("Testing HTTP Server Started color...\n");
    rgb_led_http_server_started();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to observe color
    TEST_PASS();
}

TEST_CASE("rgb_led_wifi_connected_test", "[rgb_led]")
{
    printf("Testing WiFi Connected color...\n");
    rgb_led_wifi_connected();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to observe color
    TEST_PASS();
}

TEST_CASE("rgb_led_ap_client_connected_test", "[rgb_led]")
{
    printf("Testing AP Client Connected color...\n");
    rgb_led_ap_client_connected();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to observe color
    TEST_PASS();
}

TEST_CASE("rgb_led_ap_client_disconnected_test", "[rgb_led]")
{
    printf("Testing AP Client Disconnected color...\n");
    rgb_led_ap_client_disconnected();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to observe color
    TEST_PASS();
}
