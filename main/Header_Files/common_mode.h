
#ifndef COMMON_MODE_H
#define COMMON_MODE_H

#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "sdkconfig.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ble_comm_struct.h"


#define START_PERIODIC_ADV  (1<<2)           // Start periodic adv notification bit


#define EXT_ADV_HANDLE_CENTRAL 0
#define EXT_ADV_HANDLE_PERIPHERAL 1
#define EXT_ADV_HANDLE_CENTRAL_BEACON 2
#define EXT_ADV_HANDLE_PERIPHERAL_BEACON 3


#define NUM_EXT_ADV 1
#define EXT_SCAN_DURATION 0
#define EXT_SCAN_PERIOD 0

#define GPIO_INPUT_4  4
#define GPIO_INPUT_5  5
#define GPIO_INPUT_6  6
#define GPIO_INPUT_7  7

static const gpio_num_t input_pins[] = {
    GPIO_INPUT_4, GPIO_INPUT_5, GPIO_INPUT_6, GPIO_INPUT_7
};


#define FUNC_SEND_WAIT_SEM(func, sem) do {\
    esp_err_t __err_rc = (func);\
    if (__err_rc != ESP_OK) { \
        ESP_LOGE(LOG_TAG_Common, "%s, message send fail, error = %d", __func__, __err_rc); \
    } \
    xSemaphoreTake(sem, portMAX_DELAY); \
} while(0);



//extern uint8_t mac_addr[6]; 

extern uint8_t SID;

void init_gpio_inputs(void);
void ble_get_mac_address(uint8_t *mac);  


void share_type_info(char * type,char code); // share the type and code with other modules

void on_any_event_trigger();
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

extern TaskHandle_t deferredCentralStartTaskHandle;
void deferredCentralStartTask(void *pvParam);
void adv_command_executor_task(void *param);


#define LOG_TAG_Peripheral "PERIPHERAL_ADV_DEMO"
#define LOG_TAG_Central    "CENTRAL_ADV_DEMO"
#define LOG_TAG_Common     "COMMON_ADV_DEMO"
#define LOG_TAG_Sensor     "SENSOR_MODULE"
#define LOG_TAG_NTP        "NTP"



#endif