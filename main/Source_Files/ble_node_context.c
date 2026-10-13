#include "ble_node_context.h"

void init_common_context(void) {

    ble_node_ctx.device.power = usb;
    ble_node_ctx.device.wifi = No_wifi;
    ble_node_ctx.device.sync = Not_synced;

    ble_node_ctx.best_central.parent_code = 255;  // Initialize to max value
    ble_node_ctx.best_central.is_valid = false;

    ble_node_ctx.my_current_role = ROLE_UNDEFINED;
    ble_node_ctx.my_new_role = ROLE_UNDEFINED;
    ble_node_ctx.last_known_role = ROLE_UNDEFINED;
    ble_node_ctx.shared_code = 0;
    ble_node_ctx.test_sem = NULL;
    memset(ble_node_ctx.mac_addr, 0, sizeof(ble_node_ctx.mac_addr));

    /*ble_node_ctx.adv_status.ext_adv_beacon_started = false;
    ble_node_ctx.adv_status.ext_adv_beacon_data_raw = false;
    ble_node_ctx.adv_status.ext_adv_beacon_set_params = false;  */

    memset(ble_node_ctx.adv_status.ext_adv_started, 0, sizeof(ble_node_ctx.adv_status.ext_adv_started));
    memset(ble_node_ctx.adv_status.ext_adv_data_raw, 0, sizeof(ble_node_ctx.adv_status.ext_adv_data_raw));
    memset(ble_node_ctx.adv_status.ext_adv_set_params, 0, sizeof(ble_node_ctx.adv_status.ext_adv_set_params));

    memset(ble_node_ctx.adv_status.periodic_adv_data_raw, 0, sizeof(ble_node_ctx.adv_status.periodic_adv_data_raw));
    memset(ble_node_ctx.adv_status.periodic_adv_set_params, 0, sizeof(ble_node_ctx.adv_status.periodic_adv_set_params));
    memset(ble_node_ctx.adv_status.periodic_adv_started, 0, sizeof(ble_node_ctx.adv_status.periodic_adv_started));

    ble_node_ctx.adv_status.ext_scan_started = false;
    ble_node_ctx.adv_status.ext_scan_params_set = false;

    ble_node_ctx.adv_wait.cmd_result = false;
    ble_node_ctx.adv_wait.pending_cmd = ADV_CMD_NONE;

    ble_node_ctx.adv_message.command = ADV_CMD_NONE;
    memset(ble_node_ctx.adv_message.data, 0, sizeof(ble_node_ctx.adv_message.data));
    ble_node_ctx.adv_message.data_len = 0;
    ble_node_ctx.adv_message.term_sync = 0;
    ble_node_ctx.SID = 0;

    ble_node_ctx.repeater_role_demote.central_present = false;
    ble_node_ctx.repeater_role_demote.peripheral_present = false;

    ble_node_ctx.wifi_info.wifi_Get_type = No_wifi;
    //  ble_node_ctx.wifi_info.wifi_state = WIFI_DISCONNECTED_ADVERTISER_MODE;
    ble_node_ctx.wifi_info.wifi_version_string = NULL;

    ble_node_ctx.device_info_queue = NULL;

    ble_node_ctx.retry_timer = NULL;
    ble_node_ctx.wifi_timer_stability_timer = NULL;
    ble_node_ctx.ext_adv_ack_timer = NULL;

    // NTP
    ble_node_ctx.utc_time_update.utc_seconds = 0;
    ble_node_ctx.utc_time_update.ble_message_type = BLE_NORMAL_MESSAGE;
    ble_node_ctx.utc_time_update.ble_time_update = TIME_NONE;

    /* ble_node_ctx.transition_action = TRANS_NONE;
    ble_node_ctx.role_transfer.from = ROLE_UNDEFINED;
    ble_node_ctx.role_transfer.to = ROLE_UNDEFINED;

 */
}
void init_central_context(void) {

    ble_node_ctx.central_adv_handle_array[0] = EXT_ADV_HANDLE_CENTRAL;
    // ble_node_ctx.current_log_level = Print_All_log;
    ble_node_ctx.central_sync_worker_TaskHandle = NULL;
    ble_node_ctx.central_syncQueue = NULL;
    ble_node_ctx.central_periodic_adv_updateTaskHandle = NULL;
    ble_node_ctx.central_auto_stop_ext_adv = false;
    ble_node_ctx.central_adv_timer = NULL;
    ble_node_ctx.central_PA_timer = NULL;
    // ble_node_ctx.central_ext_beacon_adv_started = false;
    memset(ble_node_ctx.peripheral_child_mac, 0, sizeof(ble_node_ctx.peripheral_child_mac));

    ble_node_ctx.adv_message.handle_ext = EXT_ADV_HANDLE_CENTRAL;
    ble_node_ctx.is_repeater_role_demoted = false;
    ble_node_ctx.time_request_handled = false;
    ble_node_ctx.ext_adv_detected = false;  // Flag to indicate if extended advertising detected
}
void init_peripheral_context(void) {

    ble_node_ctx.adv_status.node_scan_active = false;
    ble_node_ctx.peripheral_auto_stop_ext_adv = false;
    ble_node_ctx.peripheral_ack_type = ACK_Type_NONE;
    ble_node_ctx.peripheral_ext_adv_started = false;
    ble_node_ctx.peripheral_periodic_adv_updateTaskHandle = NULL;
    // ble_node_ctx.peripheral_ext_beacon_adv_started = false;
    memset(ble_node_ctx.peripheral_parent_mac, 0, sizeof(ble_node_ctx.peripheral_parent_mac));
    ble_node_ctx.peripheral_Ext_adv_timer = NULL;
    ble_node_ctx.peripheral_PA_timer = NULL;
    ble_node_ctx.peripheral_PA_Data_update_timer = NULL;
    ble_node_ctx.peripheral_sync_bestCentral_delay_timer = NULL;
    ble_node_ctx.peripheral_scanning_event = 0;
    ble_node_ctx.peripheral_scanning_queue = NULL;

    ble_node_ctx.adv_message.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
    ble_node_ctx.cleanup_task_handle = NULL;
    ble_node_ctx.is_cleanup_started = false;
    ble_node_ctx.cleanup_type = cleanup_none;
    ble_node_ctx.time_response_handled = false;
}

void init_ble_comm_struct_context(ble_node_context_t *ctx) {

    uint8_t periodic_data[] = {0x02, 0x01, 0x06, 0x02, 0x0a, 0xeb, 0x03, 0x03, 0xab, 0xcd, 0x12, 0x09, 'E', 'S', 'P', '_', 'P', 'E', 'R', 'I', 'O', 'D', 'I', 'C', '_', 'A', 'D', 'V', 'X', 'S'};

    memcpy(ctx->periodic_adv_raw_data, periodic_data, sizeof(periodic_data));
    ctx->periodic_adv_raw_data_len = sizeof(periodic_data);

    memcpy(ctx->periodic_beacon_data, periodic_data, sizeof(periodic_data));
    ctx->periodic_beacon_data_len = sizeof(periodic_data);

    const char *beaon_name = "ESP_BEACON_ADVX";
    uint8_t beacon_name_len = strlen(beaon_name);

    ctx->periodic_beacon_data[10] = beacon_name_len + 1;  // Update the length of the device name in the periodic beacon data
    ctx->periodic_beacon_data[11] = 0x09;                 // Update the AD type to "Complete Local Name" in the periodic beacon data

    memcpy(&ctx->periodic_beacon_data[12], beaon_name, beacon_name_len);  // Update the device name in the periodic beacon data
    ctx->periodic_beacon_data_len = 12 + beacon_name_len;                 // Update the total length of the periodic beacon data

    uint8_t ack_data_local[] = {

        0x02, 0x01, 0x06, 0x02, 0x0a, 0xeb, 0x0F, 0x09, 'C', 'E', 'N', '_',
        'A',  'C',  'K',  '_',  'B',  'E',  'A',  'C',  'O', 'N'

    };

    memcpy(ctx->ack_data, ack_data_local, sizeof(ack_data_local));

    uint8_t ext_adv[] = {
        0x02, 0x01, 0x06, 0x02, 0x0a, 0xeb, 0x14, 0x09, 'E', 'S', 'P', '_', 'M', 'U', 'L', 'T', 'I', '_', 'A', 'D', 'V', '_', '8', '0', 'M', 'S', '!',

        0x07, 0xFF, 0xFF, 0x01, 0x02, 0x00, 0x00, 0x00  // Manufacturer specific data to indicate the node capability for repeater role and also used by central to identify , sendinng the device code
                                                        // @byte number 31
    };  // Message type  , Time status , UTC seconds

    memcpy(ctx->raw_ext_adv_data_2m, ext_adv, sizeof(ext_adv));
}

void init_ble_node_context() {

    memset(&ble_node_ctx, 0, sizeof(ble_node_context_t));
    // memset(ble_node_ctx.tracked_devices, 0, sizeof(ble_node_ctx.tracked_devices));
    init_common_context();
    // init_central_context();
    // init_peripheral_context();
    init_ble_comm_struct_context(&ble_node_ctx);
}
