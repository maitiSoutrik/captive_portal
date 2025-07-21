#include "driver/gpio.h"
#include "dht22_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

#define DHT22_GPIO 23
#define DHT22_DATA_BITS 40
#define DHT22_DATA_BYTES 5
#define DHT22_TIMER_INTERVAL 2000
#define DHT22_TIMEOUT 10000

static const char *TAG = "DHT22";

static float temperature = -100.0f;
static float humidity = -1.0f;
static portMUX_TYPE dht22_mux = portMUX_INITIALIZER_UNLOCKED;

static int wait_for_level(int level, int timeout_us)
{
    int ticks = 0;
    while (gpio_get_level(DHT22_GPIO) != level)
    {
        if (ticks++ > timeout_us)
        {
            return -1;
        }
        ets_delay_us(1);
    }
    return 0;
}

static int get_data(uint8_t *data)
{
    int i, j = 0, bit = 0;
    uint8_t byte = 0;

    portENTER_CRITICAL(&dht22_mux);

    gpio_set_direction(DHT22_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT22_GPIO, 0);
    ets_delay_us(1100);
    gpio_set_level(DHT22_GPIO, 1);
    ets_delay_us(30);
    gpio_set_direction(DHT22_GPIO, GPIO_MODE_INPUT);

    if (wait_for_level(0, DHT22_TIMEOUT) != 0)
    {
        portEXIT_CRITICAL(&dht22_mux);
        return -1;
    }
    if (wait_for_level(1, DHT22_TIMEOUT) != 0)
    {
        portEXIT_CRITICAL(&dht22_mux);
        return -1;
    }
    if (wait_for_level(0, DHT22_TIMEOUT) != 0)
    {
        portEXIT_CRITICAL(&dht22_mux);
        return -1;
    }

    for (i = 0; i < DHT22_DATA_BITS; i++)
    {
        if (wait_for_level(1, DHT22_TIMEOUT) != 0)
        {
            portEXIT_CRITICAL(&dht22_mux);
            return -1;
        }
        ets_delay_us(35);
        bit = gpio_get_level(DHT22_GPIO);
        byte = (byte << 1) | bit;
        if ((i + 1) % 8 == 0)
        {
            data[j] = byte;
            j++;
            byte = 0;
        }
        if (wait_for_level(0, DHT22_TIMEOUT) != 0)
        {
            portEXIT_CRITICAL(&dht22_mux);
            return -1;
        }
    }

    portEXIT_CRITICAL(&dht22_mux);
    return 0;
}

static void dht22_task(void *pvParameters)
{
    uint8_t data[DHT22_DATA_BYTES];
    while (1)
    {
        if (get_data(data) == 0)
        {
            uint8_t checksum = data[0] + data[1] + data[2] + data[3];
            if (checksum == data[4])
            {
                portENTER_CRITICAL(&dht22_mux);
                humidity = (data[0] << 8 | data[1]) / 10.0f;
                temperature = ((data[2] & 0x7F) << 8 | data[3]) / 10.0f;
                if (data[2] & 0x80)
                {
                    temperature *= -1;
                }
                portEXIT_CRITICAL(&dht22_mux);
                // ESP_LOGI(TAG, "Humidity: %.1f%%, Temperature: %.1fC", humidity, temperature);
            }
            else
            {
                ESP_LOGE(TAG, "Checksum error");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read data from sensor");
        }
        vTaskDelay(pdMS_TO_TICKS(DHT22_TIMER_INTERVAL));
    }
}

esp_err_t dht22_init(void)
{
    gpio_reset_pin(DHT22_GPIO);
    BaseType_t task_result = xTaskCreate(dht22_task, "dht22_task", 2048, NULL, 5, NULL);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dht22_task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

float dht22_get_temperature(void)
{
    float temp;
    portENTER_CRITICAL(&dht22_mux);
    temp = temperature;
    portEXIT_CRITICAL(&dht22_mux);
    return temp;
}

float dht22_get_humidity(void)
{
    float hum;
    portENTER_CRITICAL(&dht22_mux);
    hum = humidity;
    portEXIT_CRITICAL(&dht22_mux);
    return hum;
}
