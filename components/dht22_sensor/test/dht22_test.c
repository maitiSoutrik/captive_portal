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

    // Error handling: simulate or check for sensor read failure (if possible)
    float temp = dht22_get_temperature();
    printf("Temperature: %.1fC\n", temp);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(-100.0f, temp, "Sensor returned error value for temperature");

    // Multiple consecutive readings for consistency
    float temps[5];
    for (int i = 0; i < 5; ++i) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        temps[i] = dht22_get_temperature();
        printf("Reading %d: %.1fC\n", i+1, temps[i]);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(-100.0f, temps[i], "Sensor returned error value for temperature on repeated read");
        TEST_ASSERT_TRUE_MESSAGE(temps[i] > -40.0f && temps[i] < 80.0f, "Temperature out of valid range on repeated read");
    }
    // Consistency check: difference between consecutive readings should not be extreme
    for (int i = 1; i < 5; ++i) {
        float diff = temps[i] - temps[i-1];
        TEST_ASSERT_TRUE_MESSAGE(fabsf(diff) < 10.0f, "Unrealistic jump between consecutive temperature readings");
    }

    // Boundary condition checks (simulate or check for min/max values)
    // Here, we check that the sensor never returns values outside the valid range
    TEST_ASSERT_TRUE_MESSAGE(temp >= -40.0f, "Temperature below minimum valid value");
    TEST_ASSERT_TRUE_MESSAGE(temp <= 80.0f, "Temperature above maximum valid value");
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

// Error handling test: simulate uninitialized sensor and hardware failure
TEST_CASE("dht22 sensor error handling scenarios", "[dht22][error]")
{
    // Simulate uninitialized sensor by not calling setup_sensor()
    float temp_uninit = dht22_get_temperature();
    float hum_uninit = dht22_get_humidity();
    printf("[Uninitialized] Temperature: %.1fC, Humidity: %.1f%%\n", temp_uninit, hum_uninit);
    // Should return error values, not crash
    TEST_ASSERT_EQUAL(-100.0f, temp_uninit);
    TEST_ASSERT_EQUAL(-1.0f, hum_uninit);

    // Simulate hardware failure by mocking get_data to always fail (if possible)
    // This is a placeholder: in a real test, use a mock framework or dependency injection
    // For now, we can at least check that repeated failed reads do not crash or return out-of-range values
    for (int i = 0; i < 3; ++i) {
        float temp = dht22_get_temperature();
        float hum = dht22_get_humidity();
        TEST_ASSERT_TRUE((temp == -100.0f) || (temp > -40.0f && temp < 80.0f));
        TEST_ASSERT_TRUE((hum == -1.0f) || (hum >= 0.0f && hum <= 100.0f));
    }
    // If mocking is available, insert mock here to force get_data failure and assert error values
}
