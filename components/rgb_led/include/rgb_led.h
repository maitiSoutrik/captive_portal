/*
 * rgb_led.h
 *
 *  Created on: Oct 11, 2021
 *      Author: kjagu
 */

#ifndef MAIN_RGB_LED_H_
#define MAIN_RGB_LED_H_

// RGB LED GPIOs
#define RGB_LED_RED_GPIO		5
#define RGB_LED_GREEN_GPIO		18
#define RGB_LED_BLUE_GPIO		19

// RGB LED color mix channels
#define RGB_LED_CHANNEL_NUM		3

// RGB LED configuration
typedef struct
{
	int channel;
	int gpio;
	int mode;
	int timer_index;
} ledc_info_t;

/**
 * Color to indicate WiFi application has started.
 */
void rgb_led_wifi_app_started(void);

/**
 * Color to indicate HTTP server has started.
 */
void rgb_led_http_server_started(void);

/**
 * Color to indicate that the ESP32 is connected to an access point.
 */
void rgb_led_wifi_connected(void);

/**
 * Color to indicate that an access point client has connected.
 */
void rgb_led_ap_client_connected(void);

/**
 * Color to indicate that the access point client has been disconnected.
 */
void rgb_led_ap_client_disconnected(void);

#endif /* MAIN_RGB_LED_H_ */
