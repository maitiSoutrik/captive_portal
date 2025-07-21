#include <stdio.h>
#include "unity.h"
#include "dht22_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Helper function to initialize and wait for sensor readings
static void setup_sensor(void)
{
    static bool initialized = false;
    if (!initialized)
    {
        TEST_ASSERT_EQUAL(ESP_OK, dht22_init());
        // Wait for the sensor to perform a few readings
        vTaskDelay(pdMS_TO_TICKS(10000));
        initialized = true;
    }
}

TEST_CASE("dht22 sensor reads temperature data", "[dht22]")
{
    setup_sensor();
    float temp = dht22_get_temperature();
    printf("Temperature: %.1fC\n", temp);

    // Basic sanity check
    TEST_ASSERT_NOT_EQUAL(-100.0f, temp);
    TEST_ASSERT_TRUE(temp > -40.0f && temp < 80.0f);
}

TEST_CASE("dht22 sensor reads humidity data", "[dht22]")
{
    setup_sensor();
    float hum = dht22_get_humidity();
    printf("Humidity: %.1f%%\n", hum);

    // Basic sanity check
    TEST_ASSERT_NOT_EQUAL(-1.0f, hum);
    TEST_ASSERT_TRUE(hum >= 0.0f && hum <= 100.0f);
}
