
#ifndef WIFI_H
#define WIFI_H
#include "device_type_utils.h"
#include "esp_wifi.h"
#include "log.h"


#define Wifi_Read_Via_Buttons 1
#define Wifi_Read_Via_Scan 0

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1
#define WIFI_STABILITY_BIT BIT2

#define LOG_TAG_Wifi "Wifi_MODULE"
extern char retries;

extern TaskHandle_t role_task_handle;

// 1. State Management , code added

/*  typedef enum {

    WIFI_CONNECTED_SCANNER_MODE,
    WIFI_DISCONNECTED_ADVERTISER_MODE

}wifi_state_t;

 */

extern char *wifi_version_strings[4];

typedef struct {

    wifi_type_t wifi_Get_type;
    //   wifi_state_t wifi_state;
    char *wifi_version_string;

} wifi_info_t;

void initialize_wifi();
// void start_retry_timer(ble_node_context_t *ctxz);
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void configure_wifi();
void log_wifi_version(wifi_type_t wifi_Get_type);
wifi_type_t get_wifi_ap_info();
void retry_timer_callback();
void Role_Update_Task(void *pvParameters);
void Read_Wifi_Status(void *pvParam);

#endif