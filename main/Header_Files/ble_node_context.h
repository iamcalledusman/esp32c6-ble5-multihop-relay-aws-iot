
#ifndef BLE_NODE_CONTEXT_H
#define BLE_NODE_CONTEXT_H

#include "ble_comm_struct.h"
#include "device_type_utils.h"
#include "log.h"
#include "time_sync.h"
#include "wifi.h"

// #include "central_mode.h"
#define CENTRAL_MAX_PERIODIC_DEVICES 3  // maximum number of devices to track

void init_ble_node_context(void);
void init_common_context(void);
void init_central_context(void);
void init_peripheral_context(void);

typedef struct {

    periodic_device_info_t central_tracked_devices[CENTRAL_MAX_PERIODIC_DEVICES];  // periodic device info structure for central role
    periodic_device_info_t peripheral_master_device;                               // periodic device info structure for peripheral role
    // central
    QueueHandle_t central_syncQueue;
    TaskHandle_t central_sync_worker_TaskHandle;
    esp_timer_handle_t central_adv_timer;                 // timer handle for advertising timer
    esp_timer_handle_t central_one_second_timer;          // timer handle for one second timer
    esp_timer_handle_t central_three_seconds_timer_scan;  // timer handle for one second timer
    esp_timer_handle_t central_PA_timer;                  // timer handle for periodic advertising timer
    bool central_auto_stop_ext_adv;                       // control whether timer should be used
    uint8_t peripheral_child_mac[6];

    TaskHandle_t central_pa_ack_syncTaskHandle;
    TaskHandle_t central_periodic_adv_updateTaskHandle;  // Notify the central_pa_ack_sync_task(ack ,sync ,adv_data[] to send the prph/repeater )
    uint8_t central_adv_handle_array[1];
    bool time_request_handled;
    bool ext_adv_detected;  // Flag to indicate if extended advertising detected

    // bool central_ext_beacon_adv_started;
    //  log_level_t current_log_level;

    // peripheral
    best_central_info_t best_central;

    TaskHandle_t peripheral_ack_sync_ext_advTaskHandle;
    TaskHandle_t adv_command_executortask_handle;
    bool peripheral_auto_stop_ext_adv;  // control whether timer should be used
    bool peripheral_ext_adv_started;
    // bool peripheral_ext_beacon_adv_started;

    QueueHandle_t peripheral_scanning_queue;     // Queue to hold sync requests
    Scanning_event_t peripheral_scanning_event;  // Variable to hold the current scanning event
    ACK_Type_t peripheral_ack_type;
    esp_timer_handle_t peripheral_sync_bestCentral_delay_timer;  // timer handle for sync delay timer
    esp_timer_handle_t peripheral_PA_timer;
    esp_timer_handle_t peripheral_PA_Data_update_timer;
    esp_timer_handle_t peripheral_Ext_adv_timer;
    esp_timer_handle_t peripheral_one_second_timer;  // timer handle for one second timer
    uint8_t peripheral_parent_mac[6];                // parent mac address to be used in the adv data
    cleanup_type_t cleanup_type;
    bool is_cleanup_started;
    TaskHandle_t cleanup_task_handle;
    TaskHandle_t peripheral_periodic_adv_updateTaskHandle;
    bool time_response_handled;

    // common

    SemaphoreHandle_t test_sem;
    uint8_t mac_addr[6];  // Array to store BLE MAC Address
    Adv_Status_t adv_status;
    adv_wait_state_t adv_wait;
    uint8_t SID;
    QueueHandle_t adv_queue;  // Queue to hold advertising messages for central and peripheral roles
    QueueHandle_t device_info_queue;
    Repeater_Role_Demote_t repeater_role_demote;
    bool is_repeater_role_demoted;

    adv_msg_t adv_message;
    // uint8_t handle_ext;
    TimerHandle_t retry_timer;
    TimerHandle_t wifi_timer_stability_timer;  // Timer to check Wi-Fi stability after connection
    esp_timer_handle_t ext_adv_ack_timer;      // Timer to check if ACK is received from the central device after starting extended advertising

    wifi_info_t wifi_info;

    // device_type_utlis

    device_role_t my_current_role, my_new_role, last_known_role;
    uint8_t shared_code;                     // Global variable to hold the shared code
    device_type_t look_up_table[max_types];  // Array to hold device type information
    device_type_t device;                    // Global variable to hold the device information , power , wifi , sync , type , code

    // ble communication struct

    uint8_t periodic_adv_raw_data[ADV_PAYLOAD_MAX_LEN];  // Buffer to hold periodic advertising raw data
    size_t periodic_adv_raw_data_len;                    // known size for safe use

    uint8_t periodic_beacon_data[ADV_PAYLOAD_MAX_LEN];  // Buffer to hold periodic advertising raw data
    size_t periodic_beacon_data_len;                    // known size for safe use

    uint8_t ack_data[41];             // ACK data for periodic advertising
    uint8_t raw_ext_adv_data_2m[36];  // Extended advertising data

    //  transition_action_t transition_action;
    // role_transfer_t role_transfer;

    // ntp
    utc_time_update_t utc_time_update;

} ble_node_context_t;

extern ble_node_context_t ble_node_ctx;

void utc_normal_message_action_from_Peripheral(ble_node_context_t *ctx);
void utc_normal_message_action_from_Central(ble_node_context_t *ctx);

#endif  // BLE_NODE_CONTEXT_H