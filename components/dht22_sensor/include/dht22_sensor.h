#ifndef DHT22_H
#define DHT22_H

#include "esp_err.h"

/**
 * @brief Initializes the DHT22 sensor.
 *
 * This function sets up the GPIO pin for the DHT22 sensor and starts a FreeRTOS task
 * to periodically read the sensor data.
 *
 * @return esp_err_t 
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 */
esp_err_t dht22_init(void);

/**
 * @brief Gets the last read temperature from the DHT22 sensor.
 *
 * @return The temperature in degrees Celsius. Returns -100.0 if no data is available.
 */
float dht22_get_temperature(void);

/**
 * @brief Gets the last read humidity from the DHT22 sensor.
 *
 * @return The relative humidity in percent. Returns -1.0 if no data is available.
 */
float dht22_get_humidity(void);

#endif /* DHT22_H */
