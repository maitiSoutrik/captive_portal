/**
 * @file app_station.h
 */
#ifndef APP_STATION_H
#define APP_STATION_H

void wifi_init_sta(void);
esp_err_t app_station_connect_to_ap(const char *ssid, const char *password);

#endif /* APP_STATION_H */