#ifndef BLE_COMM_STRUCT_H
#define BLE_COMM_STRUCT_H

#include "common_mode.h"
#include <stdint.h>

#define ADV_PAYLOAD_MAX_LEN 30

/* typedef enum {
    STAGE_NONE = 0,
    STAGE_1_CEN_TO_PRPH_DONE,
    STAGE_2_PRPH_TO_CEN_DONE,
    STAGE_WAITING_FOR_SYNC,
} sync_stage_t;

 */

typedef enum {

    STAGE_IDLE = 0,
    STAGE_WAITING_PRPH_ACK,
    STAGE_PRPH_ACK_RECEIVED,
    STAGE_PA_SYNC_QUEUED,
    STAGE_PA_SYNCING,
    STAGE_PA_SYNCED,

} sync_stage_t;

typedef struct {  // periodic device info structure

    bool is_synced;
    bool is_syncing;
    bool in_use;
    bool is_queued;  // New flag
    uint8_t addr_type;
    esp_bd_addr_t addr;
    uint16_t sync_handle;
    uint8_t sid;

    uint8_t synced_peer_last3_mac[3];  // Store last 3 bytes of MAC from the peer device that is synced with, used for matching with periodic adv report

    bool ext_ack_pending;
    bool pa_ack_pending;

    bool sync_ack_pending;  // Flag to indicate if waiting for sync ACK after receiving periodic adv report with new data

    bool Periodic_Adv_Sync_Estab;  // Flag to indicate if periodic advertising sync established

    /* sync process from central to prph and then prph to central */
    sync_stage_t sync_stage;         // Add sync stage to track the sync process
    uint8_t discovery_last3_mac[3];  // Store last 3 bytes of MAC from discovery report, used for matching with periodic adv report

    // uint32_t last_seen_time;                               // Timestamp of the last time this device was seen

    uint8_t parent_last3_mac[3];  // Store last 3 bytes of MAC from the parent device, used for matching with periodic adv report

} periodic_device_info_t;

typedef struct {
    uint8_t sid;
    uint8_t addr_type;
    esp_bd_addr_t addr;
    uint8_t parent_code;
    bool is_valid;
} best_central_info_t;

typedef enum {  //
    Periodic_PDU_SYN = 0,
    Periodic_PDU_ACK,
    START_EXTENDED_ADV
} Scanning_event_t;  // scanning event type ,

typedef enum {

    ACK_Type_NONE = 0,
    ACK_Type_SYNC,
    ACK_Type_ACK

} ACK_Type_t;

typedef struct {  //

    bool ext_adv_started[4];
    bool ext_adv_data_raw[4];
    bool ext_adv_set_params[4];

    bool ext_scan_started;
    bool ext_scan_params_set;

    /*
        bool periodic_adv_started;
        bool periodic_adv_data_raw;
        bool periodic_adv_set_params; */

    bool periodic_adv_started[4];
    bool periodic_adv_data_raw[4];
    bool periodic_adv_set_params[4];

    bool node_scan_active;

    bool ext_adv_beacon_started[2];
    bool ext_adv_beacon_set_params[2];
    bool ext_adv_beacon_data_raw[2];

} Adv_Status_t;  // scanning event type ,

typedef enum {

    ADV_CMD_NONE = 0,
    ADV_CMD_START_EXT,

    ADV_CMD_STOP_EXT,
    ADV_CMD_UPDATE_EXT_DATA,
    ADV_CMD_SET_EXT_PARAMS,

    ADV_CMD_REMOVE_EXT_PARAMS,

    ADV_CMD_START_SCAN,
    ADV_CMD_STOP_SCAN,
    ADV_CMD_SET_SCAN,

    ADV_CMD_SET_PA_PARAMS,
    ADV_CMD_START_PA,
    ADV_CMD_STOP_PA,
    ADV_CMD_UPDATE_PA_DATA,

    ADV_CMD_TERMINATE_SYNC,

} adv_command_t;  // advertising command type

typedef struct {

    bool cmd_result;
    adv_command_t pending_cmd;

} adv_wait_state_t;

typedef struct {
    adv_command_t command;  // which action to perform
    uint8_t handle_ext;     // which handle to use for the command
    uint8_t data[41];       // optional payload pointer
    size_t data_len;        // length of payload
    uint16_t term_sync;
} adv_msg_t;

typedef enum {

    cleanup_none = 0,
    cleanup_PA_timeout,
    cleanup_SYNC_lost,

} cleanup_type_t;

typedef struct {

    bool central_present;
    bool peripheral_present;

} Repeater_Role_Demote_t;

extern volatile bool ack_ready;  // Global flag to track when to send ACK

#endif