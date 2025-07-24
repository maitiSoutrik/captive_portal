#ifndef COMPONENTS_AWS_IOT_INCLUDE_AWS_IOT_H_
#define COMPONENTS_AWS_IOT_INCLUDE_AWS_IOT_H_

#define CONFIG_AWS_EXAMPLE_CLIENT_ID "RopeIoT_ESP32_Test_Policy"
/**
 * Starts AWS IoT task.
 */
#include "esp_err.h"

esp_err_t aws_iot_start(void);

#endif /* COMPONENTS_AWS_IOT_INCLUDE_AWS_IOT_H_ */
