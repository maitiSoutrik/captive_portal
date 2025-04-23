/**
 * @file local_server.h
 */
#ifndef LOCAL_SERVER_H
#define LOCAL_SERVER_H

/*
 * Messages for the HTTP Monitor
 */
typedef enum http_server_msg
{
    HTTP_MSG_WIFI_CONNECT_INIT = 0,
    HTTP_MSG_WIFI_CONNECT_SUCCESS,
    HTTP_MSG_WIFI_CONNECT_FAIL,
    HTTP_MSG_WIFI_USER_DISCONNECT,
    HTTP_MSG_WIFI_OTA_UPDATE_SUCCESSFUL,
    HTTP_MSG_WIFI_OTA_UPDATE_FAILED,
    HTTP_MSG_TIME_SERVICE_INITIALIZED,
} http_server_msg_e;

/*
 * Connection Status for WiFi
 */
typedef enum http_server_wifi_connect_status
{
    HTTP_WIFI_STATUS_CONNECT_NONE = 0,
    HTTP_WIFI_STATUS_CONNECTING,
    HTTP_WIFI_STATUS_CONNECT_FAILED,
    HTTP_WIFI_STATUS_CONNECT_SUCCESS,
    HTTP_WIFI_STATUS_DISCONNECTED,
} http_server_wifi_connect_status_e;

/*
 * Structure for Message Queue
 */
typedef struct http_server_q_msg
{
    http_server_msg_e msg_id;
} http_server_q_msg_t;

void http_server_fw_update_reset_cb(void *arg);

#endif // LOCAL_SERVER_H