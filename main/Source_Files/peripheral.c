#include "ble_comm_struct.h"
#include "ble_node_context.h"
#include "ble_sync_utils.h"
#include "common_mode.h"
#include "device_type_utils.h"
#include "log.h"
#include "ntp.h"
#include "peripheral_mode.h"


#include <sys/time.h>
#include <time.h>

// periperal role

#define PERIPHERAL_MAX_PERIODIC_DEVICES 4

esp_ble_gap_ext_adv_params_t ext_adv_params_2M_Peripheral = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
    .interval_min = 2000,  // 4000 * 0.625 ms = 2500 ms
    .interval_max = 2000,  // 4000 * 0.625 ms = 2500 ms

    .channel_map = ADV_CHNL_ALL,
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    .primary_phy = ESP_BLE_GAP_PHY_CODED,
    .max_skip = 0,
    .secondary_phy = ESP_BLE_GAP_PHY_CODED,
    .sid = 0,
    .scan_req_notif = false,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .tx_power = EXT_ADV_TX_PWR_NO_PREFERENCE,
};

esp_ble_gap_ext_adv_params_t ext_adv_params_beacon_Peripheral = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
    .interval_min = 24000,  // 24000 * 0.625 ms = 15000 ms
    .interval_max = 24000,  // 24000 * 0.625 ms = 15000 ms

    .channel_map = ADV_CHNL_ALL,
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    .primary_phy = ESP_BLE_GAP_PHY_CODED,
    .max_skip = 0,
    .secondary_phy = ESP_BLE_GAP_PHY_CODED,
    .sid = 0,
    .scan_req_notif = false,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .tx_power = EXT_ADV_TX_PWR_NO_PREFERENCE,
};

esp_ble_gap_periodic_adv_params_t periodic_adv_params_peripheral = {
    // periodic adv params
    .interval_min = 12000,  // 12000 → 12000 × 1.25 ms = 15 000 ms
    .interval_max = 12000,
    .properties = 0,
};

static esp_ble_gap_periodic_adv_sync_params_t periodic_adv_sync_params = {

    .filter_policy = 0,
    .sid = 0,
    .addr_type = BLE_ADDR_TYPE_RANDOM,
    .skip = 0,
    .sync_timeout = 6000,
};

static void pa_update_timer_cb(void *arg) {

    // periodic advertising timer callback

    ble_node_context_t *ctx = (ble_node_context_t *)arg;
    MY_LOGD(LOG_TAG_Peripheral, "Periodic Advertising Timer expired → update sensor data..");

    xTaskNotify(ctx->peripheral_periodic_adv_updateTaskHandle, START_PERIODIC_ADV, eSetBits);  // Notify the task to start periodic advertising
}

void Peripheral_init_PA_timer(ble_node_context_t *ctx) {  // in this function we initialize the periodic advertising timer

    const esp_timer_create_args_t timer_args = {

        .callback = &pa_update_timer_cb,
        .arg = ctx,  // no argument needed
        .name = "pa_update_timer"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->peripheral_PA_Data_update_timer));
}

static void periodic_adv_update_task(void *arg) {  // task to facilitate the Gap event using Notify to send start periodic adv with counter value in the adv data

    uint32_t notification_value = 0;
    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    static int counter = 0;

    for (;;) {

        xTaskNotifyWait(0, ULONG_MAX, &notification_value, portMAX_DELAY);  // ULONG_MAX set bits on exit

        if (notification_value & START_PERIODIC_ADV) {

            /* ---------------- Update counter safely ---------------- */

            if (ctx->periodic_adv_raw_data_len > 28) {

                ctx->periodic_adv_raw_data[28] = counter + 48;
                //  ctx->periodic_adv_raw_data[29] = counter+48;

                time_t now;
                struct tm utc_time;

                time(&now);

                gmtime_r(&now, &utc_time);

                MY_LOGD(LOG_TAG_NTP, "UTC PA from the peripheral ...... : %04d-%02d-%02d %02d:%02d:%02d:%02d", utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday, utc_time.tm_hour,
                        utc_time.tm_min, utc_time.tm_sec, counter);

                ctx->periodic_adv_raw_data[29] = utc_time.tm_sec;
            }

            if (ctx->periodic_beacon_data_len > 26) {

                ctx->periodic_beacon_data[26] = counter + 48;
            }

            counter++;

            if (counter > 9) {

                counter = 0;
            }

#if (CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH)

            adv_msg_t msg = {0};

            /* ---------------- Update PA data ---------------- */

            if (ctx->periodic_adv_raw_data_len == 0 || ctx->periodic_adv_raw_data_len > sizeof(msg.data)) {

                MY_LOGE(LOG_TAG_Peripheral, "Invalid Peripheral PA data length: %u", (unsigned int)ctx->periodic_adv_raw_data_len);

            }

            else {

                memset(&msg, 0, sizeof(msg));
                msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
                msg.command = ADV_CMD_UPDATE_PA_DATA;

                memcpy(msg.data, ctx->periodic_adv_raw_data, ctx->periodic_adv_raw_data_len);

                msg.data_len = ctx->periodic_adv_raw_data_len;

                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
                    MY_LOGE(LOG_TAG_Peripheral, "Failed to queue PERIPHERAL UPDATE_PA_DATA");
                }
            }
            // for beacon ext adv data

            if (ctx->periodic_beacon_data_len == 0 || ctx->periodic_beacon_data_len > sizeof(msg.data)) {

                MY_LOGE(LOG_TAG_Peripheral, "Invalid PERIPHERAL BEACON data length: %u", (unsigned int)ctx->periodic_beacon_data_len);
            }

            else {

                if (ctx->adv_status.ext_adv_beacon_started == true) {

                    // stop
                    memset(&msg, 0, sizeof(msg));
                    msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL_BEACON;
                    msg.command = ADV_CMD_STOP_EXT;

                    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                        MY_LOGE(LOG_TAG_Peripheral, "Failed to queue ADV PERIPHERAL STOP_EXT for beacon");
                    }
                }

                // update the beacon data with the new periodic adv data
                memset(&msg, 0, sizeof(msg));
                msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL_BEACON;
                msg.command = ADV_CMD_UPDATE_EXT_DATA;

                memcpy(msg.data, ctx->periodic_beacon_data, ctx->periodic_beacon_data_len);

                msg.data_len = ctx->periodic_beacon_data_len;

                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                    MY_LOGE(LOG_TAG_Peripheral, "Failed to queue PERIPHERAL UPDATE_EXT_DATA");
                }

                // start
                memset(&msg, 0, sizeof(msg));
                msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL_BEACON;
                msg.command = ADV_CMD_START_EXT;

                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                    MY_LOGE(LOG_TAG_Peripheral, "Failed to queue ADV PERIPHERAL START_EXT for beacon");
                }
            }
            // send data to AWS/MQTT Broker --> MQTT Explorer

#endif

#if CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH

            // **** OUTSIDE MUTEX — SAFE DELAY ****
            //     vTaskDelay(pdMS_TO_TICKS(10));
            //      MY_LOGE(LOG_TAG_Peripheral, "CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH");
            vTaskDelay(pdMS_TO_TICKS(5000));

#endif
        }
    }
}

void sync_with_best_central_cb(void *arg) {

    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    if (!ctx->best_central.is_valid) {
        MY_LOGE(LOG_TAG_Peripheral, "No valid central to sync with");
        return;
    }

    if (ctx->peripheral_master_device.is_syncing == false) {

        periodic_adv_sync_params.sid = ctx->best_central.sid;
        periodic_adv_sync_params.addr_type = ctx->best_central.addr_type;
        memcpy(periodic_adv_sync_params.addr, ctx->best_central.addr, sizeof(esp_bd_addr_t));

        esp_err_t ret = esp_ble_gap_periodic_adv_create_sync(&periodic_adv_sync_params);

        if (ret == ESP_OK) {

            ctx->peripheral_master_device.is_syncing = true;
            // MY_LOGI(LOG_TAG_Peripheral, "best central SID=%d, Code=%d", ctx->best_central.sid, ctx->best_central.parent_code);
        }

        else {

            MY_LOGE(LOG_TAG_Peripheral, "Sync creation failed: 0x%x", ret);
        }

        /*  ctx->best_central.is_valid = false;  // Reset after using
         ctx->best_central.parent_code = 255; */
    }
}

static void one_second_timer_cb(void *arg) {
    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    time_t now;
    struct tm utc_time;

    time(&now);

    gmtime_r(&now, &utc_time);

    MY_LOGD(LOG_TAG_NTP, "UTC: %04d-%02d-%02d %02d:%02d:%02d", utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday, utc_time.tm_hour, utc_time.tm_min, utc_time.tm_sec);
}
void init_sync_bestCentral_timer(ble_node_context_t *ctx) {

    const esp_timer_create_args_t timer_args = {

        .callback = &sync_with_best_central_cb, .arg = ctx, .name = "sync_best_central_timer"

    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->peripheral_sync_bestCentral_delay_timer));
}

static void adv_timeout_cb(void *arg) {

    ble_node_context_t *ctx = (ble_node_context_t *)arg;
    MY_LOGD(LOG_TAG_Peripheral, "100 s elapsed → stopping advertising timer");

    adv_msg_t msg = {0};

    /*  ctx->adv_message.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
   ctx->adv_message.command = ADV_CMD_STOP_EXT;                                // ext adv Stop
   xQueueSend(ctx->adv_queue, &ctx->adv_message, 0);  */
    /*    msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
       msg.command = ADV_CMD_STOP_EXT;                                             // ext adv Stop
       if(xQueueSend(ctx->adv_queue, &msg, 0)!=pdTRUE){

        MY_LOGE(LOG_TAG_Peripheral, "Failed to queue STOP_EXT from adv_timeout_cb");
       }
    */
}

void Prph_init_Ext_adv_timer(ble_node_context_t *ctx) {

    const esp_timer_create_args_t timer_args = {

        .callback = &adv_timeout_cb,
        .arg = ctx,  // no argument needed
        .name = "Prph_adv_timer"

    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->peripheral_Ext_adv_timer));
}

void Peripheral_One_Second_timer(ble_node_context_t *ctx) {

    const esp_timer_create_args_t timer_args = {

        .callback = &one_second_timer_cb,
        .arg = ctx,  // Argument passed
        .name = "One_Second_timer"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->peripheral_one_second_timer));
}

// in this timer we will set the time period of 40 secs , almost double + then the PA from the central , if central stops sending the PA because of role change then it will
// call the FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_sync_terminate(ctx->peripheral_master_device.sync_handle), ctx->test_sem);
//   ctx->peripheral_master_device.sync_handle = 0xFFFF;
//    ctx->peripheral_master_device.is_synced = false;    inside the call back function of this timer on timer expiry,

static void PA_adv_timeout_cb(void *arg) {

    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    if (ctx->is_cleanup_started == false) {

        ctx->is_cleanup_started = true;
        ctx->cleanup_type = cleanup_PA_timeout;
        xTaskNotifyGive(ctx->cleanup_task_handle);

    }

    else {

        MY_LOGW(LOG_TAG_Peripheral, "No active periodic adv sync to terminate. PA_adv_timeout_cb triggered.");
    }
}

void Prph_init_PA_timer(ble_node_context_t *ctx) {  // in this function we initialize the periodic advertising timer

    const esp_timer_create_args_t timer_args = {.callback = &PA_adv_timeout_cb,
                                                .arg = ctx,  // no argument needed
                                                .name = "Prph_PA_timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->peripheral_PA_timer));
}

static void
start_ext_adv_beacon(ble_node_context_t *ctx) {  // this function is to start the extended advertising with the beacon data when the device is in peripheral role and not synced with any central device

    adv_msg_t msg = {0};

    if (ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] == false) {

        // 1. Set EXT ADV params

        msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL_BEACON;
        msg.command = ADV_CMD_SET_EXT_PARAMS;

        if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

            MY_LOGE(LOG_TAG_Peripheral, "Failed to queue beacon SET_EXT_PARAMS");
            return;
        }

        // 2. UPDATE_EXT_DATA

        msg.command = ADV_CMD_UPDATE_EXT_DATA;

        memcpy(msg.data, &ctx->periodic_beacon_data[0], ctx->periodic_beacon_data_len);  // because we want to send the beacon messages and data will be the same as of the periodic adv data
        msg.data_len = ctx->periodic_beacon_data_len;

        if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

            MY_LOGE(LOG_TAG_Peripheral, "Failed to queue beacon UPDATE_EXT_DATA");
            return;
        }

        // 3. Start EXT ADV

        msg.command = ADV_CMD_START_EXT;

        if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

            MY_LOGE(LOG_TAG_Peripheral, "Failed to queue beacon START_EXT");
            return;
        }
    }
}

void gap_event_handler_Peripheral(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param, ble_node_context_t *ctx) {  // gap event handler to handle the events from the BLE stack

    switch (event) {
    // common role
    case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:  // 1
        xSemaphoreGive(ctx->test_sem);

        //  MY_LOGV(LOG_TAG_Peripheral, "Extended scanning params set, status %d", param->set_ext_scan_params.status);
        break;
    case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:  // 2
        xSemaphoreGive(ctx->test_sem);

        // MY_LOGV(LOG_TAG_Peripheral, "Extended scanning start, status %d", param->ext_scan_start.status);
        break;
    case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:  // 3
        xSemaphoreGive(ctx->test_sem);

        // MY_LOGI(LOG_TAG_Peripheral, "Extended scanning stop, status %d", param->ext_scan_stop.status);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_STOP_SCAN) {
            if (param->ext_scan_stop.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;
            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        break;

    case ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT:  // 4
        xSemaphoreGive(ctx->test_sem);

        //  MY_LOGI(LOG_TAG_Peripheral, "ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT, status %d, instance %d", param->ext_adv_set_rand_addr.status, param->ext_adv_set_rand_addr.instance);
        break;
    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT:  // 5
        xSemaphoreGive(ctx->test_sem);

        // MY_LOGV(LOG_TAG_Peripheral, "ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT, status %d, instance %d", param->ext_adv_set_params.status, param->ext_adv_set_params.instance);

        break;
    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT:  // 6
        xSemaphoreGive(ctx->test_sem);

        // MY_LOGV(LOG_TAG_Peripheral, "ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT, status %d, instance %d", param->ext_adv_set_params.status, param->ext_adv_set_params.instance);
        break;

    case ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT:
        xSemaphoreGive(ctx->test_sem);
        //  MY_LOGI(LOG_TAG_Peripheral, "ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT, status %d, instance %d", param->ext_adv_remove.status, param->ext_adv_remove.instance);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_REMOVE_EXT_PARAMS) {
            if (param->ext_adv_remove.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;
            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        break;

    case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT:  // 7
        xSemaphoreGive(ctx->test_sem);

        MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-03] ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT status %d, instance number %d" COLOR_RESET, param->ext_adv_start.status,
                param->ext_adv_start.instance_num);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_START_EXT) {
            if (param->ext_adv_start.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;

            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        if (param->ext_adv_start.status == ESP_BT_STATUS_SUCCESS && ctx->peripheral_ext_adv_started == true) {

            ctx->peripheral_auto_stop_ext_adv = false;  // reset the flag

            esp_err_t err = esp_timer_stop(ctx->peripheral_Ext_adv_timer);

            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {

                MY_LOGE(LOG_TAG_Peripheral, "Failed to stop Prph_adv_timer: %s", esp_err_to_name(err));
            }

            ESP_ERROR_CHECK(esp_timer_start_once(ctx->peripheral_Ext_adv_timer, 100000000));  // 100 seconds
        }
        break;

    case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT:  // 8
        xSemaphoreGive(ctx->test_sem);

        MY_LOGD(LOG_TAG_Peripheral, "ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT, status %d, instance number %d", param->ext_adv_stop.status, param->ext_adv_stop.instance_num);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_STOP_EXT) {

            if (param->ext_adv_stop.status == ESP_OK) {
                ctx->adv_wait.cmd_result = true;
                ctx->peripheral_ext_adv_started = false;
            } else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        break;

        // peripheral role
    case ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT:  // P1
        xSemaphoreGive(ctx->test_sem);

        //  MY_LOGI(LOG_TAG_Peripheral, "SP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT, status %d, instance %d", param->scan_rsp_set.status, param->scan_rsp_set.instance);
        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT:  // P2
        xSemaphoreGive(ctx->test_sem);

        //   MY_LOGV(LOG_TAG_Peripheral, "ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT, status %d, instance %d", param->peroid_adv_set_params.status, param->peroid_adv_set_params.instance);
        break;
    case ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT:  // P3
        xSemaphoreGive(ctx->test_sem);

        //  MY_LOGV(LOG_TAG_Peripheral, "ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT, status %d, instance %d", param->period_adv_data_set.status, param->period_adv_data_set.instance);

        break;
    case ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT:  // P4

        xSemaphoreGive(ctx->test_sem);

        MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-12] ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT, status %d, instance %d" COLOR_RESET, param->period_adv_start.status,
                param->period_adv_start.instance);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_START_PA) {

            if (param->period_adv_start.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;

            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }
        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_CANCEL_COMPLETE_EVT:  // C2
        xSemaphoreGive(ctx->test_sem);
        MY_LOGD(LOG_TAG_Peripheral, "Periodic advertising sync cancel, status %d", param->period_adv_sync_cancel.status);
        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_TERMINATE_COMPLETE_EVT:  // C3
        xSemaphoreGive(ctx->test_sem);

        MY_LOGD(LOG_TAG_Peripheral, "Periodic advertising sync terminate, status %d", param->period_adv_sync_term.status);

        ctx->repeater_role_demote.central_present = false;  // Mark central as not present

        if (ctx->adv_wait.pending_cmd == ADV_CMD_TERMINATE_SYNC) {

            if (param->period_adv_sync_term.status == ESP_OK)

                ctx->adv_wait.cmd_result = true;

            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_LOST_EVT: {  // C4 added

        xSemaphoreGive(ctx->test_sem);

        uint16_t sync_handle = param->periodic_adv_sync_lost.sync_handle;  // periodic advertising sync lost event

        MY_LOGD(LOG_TAG_Peripheral, "SYNC LOST for sync_handle: 0x%x ,peripheral_master_device.sync_handle 0x%x ", sync_handle, ctx->peripheral_master_device.sync_handle);

        if (ctx->peripheral_master_device.sync_handle == sync_handle) {

            ctx->peripheral_master_device.is_synced = false;
            ctx->peripheral_master_device.is_syncing = false;
            ctx->peripheral_master_device.sync_handle = 0xFFFF;
            ctx->repeater_role_demote.central_present = false;  // Mark central as not present

            //  memset(&ctx->peripheral_master_device,0,sizeof(ctx->peripheral_master_device));  //

            MY_LOGW(LOG_TAG_Peripheral, "PERIODIC ADV SYNC LOST (central) is 0x%x", ctx->peripheral_master_device.sync_handle);

            /*
            memset(ctx->peripheral_parent_mac, 0, sizeof(ctx->peripheral_parent_mac));  // ✅ clear stale parent
            ctx->best_central.is_valid = false;
            ctx->best_central.parent_code = 255;
            */

            if (ctx->is_cleanup_started == false) {

                ctx->is_cleanup_started = true;
                ctx->cleanup_type = cleanup_SYNC_lost;
                xTaskNotifyGive(ctx->cleanup_task_handle);
            }

            break;
        }

        break;
    }

    case ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT: {

        xSemaphoreGive(ctx->test_sem);

        MY_LOGD(LOG_TAG_Peripheral, "PERIODIC ADV STOP complete, status = %d", param->period_adv_stop.status);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_STOP_PA) {

            if (param->period_adv_stop.status == ESP_OK)

                ctx->adv_wait.cmd_result = true;

            else
                ctx->adv_wait.cmd_result = false;
        }

        break;
    }
    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_ESTAB_EVT: {  // C5
        xSemaphoreGive(ctx->test_sem);

        MY_LOGD(LOG_TAG_Peripheral, "PERIODIC ADV SYNC ESTAB EVT, status = %d", param->periodic_adv_sync_estab.status);

        uint8_t *addr = param->periodic_adv_sync_estab.adv_addr;

        uint16_t sync_handle = param->periodic_adv_sync_estab.sync_handle;
        uint8_t empty_array[3] = {0, 0, 0};
        uint8_t code;

        if (param->periodic_adv_sync_estab.status == ESP_OK) {

            if (memcmp(periodic_adv_sync_params.addr, addr, 6) == 0 && periodic_adv_sync_params.sid == param->periodic_adv_sync_estab.sid) {

                ctx->peripheral_master_device.is_synced = true;
                ctx->peripheral_master_device.Periodic_Adv_Sync_Estab = true;  // set the periodic adv sync established flag to true
                ctx->peripheral_master_device.sync_handle = sync_handle;

                if (memcmp(ctx->peripheral_master_device.parent_last3_mac, empty_array, 3) == 0) {
                    memcpy(ctx->peripheral_master_device.parent_last3_mac, &addr[3], 3);

                    ctx->peripheral_master_device.sync_ack_pending = true;
                }

                MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-18] PRPH_SYNC_ESTABLISHED_TO_PARENT_PA sid %d" COLOR_RESET, ctx->peripheral_master_device.sid);

                ctx->repeater_role_demote.central_present = true;                                            // Mark central as present
                ctx->peripheral_scanning_event = Periodic_PDU_SYN;                                           // Create a scanning event for periodic PDU SYN
                xQueueSend(ctx->peripheral_scanning_queue, &ctx->peripheral_scanning_event, portMAX_DELAY);  // Send the event to the scanning queue
            }
        }

        break;
    }

    case ESP_GAP_BLE_EXT_ADV_REPORT_EVT: {  // C6

        xSemaphoreGive(ctx->test_sem);  // need to look into this later on , very important

#define ADV_NAME_NORMAL "ESP_MULTI_ADV_80MS!"

        uint8_t *adv_name = NULL;
        uint8_t adv_name_len = 0;
        uint8_t parent_code = 0, new_code = 255;
        char *received_parent_type, code;

        adv_msg_t msg = {0};

        esp_ble_gap_ext_adv_report_t *report = &param->ext_adv_report.params;  // Get the extended advertising report parameters

        adv_name = esp_ble_resolve_adv_data(param->ext_adv_report.params.adv_data, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);

        MY_LOGD(LOG_TAG_Peripheral, "Ext Adv Report: MAC=%02X:%02X:%02X:%02X:%02X:%02X, SID=%d", report->addr[0], report->addr[1], report->addr[2], report->addr[3], report->addr[4], report->addr[5],
                report->sid);

        MY_LOGD(LOG_TAG_Peripheral, "adv name = %.*s", adv_name_len, adv_name ? (char *)adv_name : "N/A");

        ESP_LOG_BUFFER_HEX(LOG_TAG_Peripheral, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len);

        // ESP_LOG_BUFFER_HEX("Extended Hex with central code",&param->ext_adv_report.params.adv_data[26],param->ext_adv_report.params.adv_data_len-26);

        if (report->sid > 15) {

            MY_LOGW(LOG_TAG_Peripheral, "Ignoring SID=%d (not 0-3)", report->sid);
            //  break;
            return;  // Ignore devices with SID > 15
        }

        if (report->adv_data_len > EXT_ADV_UTC_SECONDS_INDEX && report->adv_data[EXT_ADV_MESSAGE_TYPE_INDEX] == BLE_TIME_RESPONSE_MESSAGE &&
            report->adv_data[EXT_ADV_TIME_STATUS_INDEX] == TIME_UPDATED &&
            report->adv_data[EXT_ADV_UTC_SECONDS_INDEX] <=
                59) {  // && !ctx->time_response_handled){  // means it least 35 bytes should be received in the adv data and the message type should be BLE_TIME_RESONSE_MESSAGE

            //
            if (ctx->my_current_role == ROLE_PERIPHERAL) {  // if role is peripheral then  we will update the peripheral time with the central time just received

                ctx->utc_time_update.ble_message_type =
                    (ble_message_type_t)report->adv_data[EXT_ADV_MESSAGE_TYPE_INDEX];  // store the message type received from the central device in the context structure

                ctx->utc_time_update.utc_seconds = report->adv_data[EXT_ADV_UTC_SECONDS_INDEX];

                utc_normal_message_action_from_Central(ctx);
            }

            if (!ctx->time_response_handled) {

                ctx->time_response_handled = true;  // set the flag to true to indicate that the time response has been handled

                //  ESP_ERROR_CHECK(esp_timer_start_periodic(ctx->peripheral_one_second_timer, 1000000)); //1 second
            }
        }

        size_t adv_data_len = strlen(ADV_NAME_NORMAL);

        if ((adv_name != NULL) && (adv_name_len == adv_data_len) && (memcmp(adv_name, ADV_NAME_NORMAL, adv_data_len) == 0)) {

            MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-13] PRPH_RX_CENTRAL_NORMAL_ADV" COLOR_RESET);

            new_code = param->ext_adv_report.params.adv_data[32];

            MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-14] PRPH_PARENT_CANDIDATE_COMPARE: new=%d old=%d valid=%d" COLOR_RESET, new_code, ctx->best_central.parent_code,
                    ctx->best_central.is_valid);

            if (new_code != 0 && new_code < 10) {  // // valid code range 1-9

                if (!ctx->best_central.is_valid || new_code > ctx->best_central.parent_code) {

                    ctx->best_central.sid = report->sid;
                    ctx->best_central.addr_type = report->addr_type;
                    memcpy(ctx->best_central.addr, report->addr, sizeof(esp_bd_addr_t));
                    ctx->best_central.parent_code = new_code;
                    ctx->best_central.is_valid = true;

                    MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-15] PRPH_BEST_PARENT_SELECTED: (code=%d): MAC=%02X:%02X:%02X:%02X:%02X:%02X, SID=%d" COLOR_RESET, new_code, report->addr[0],
                            report->addr[1], report->addr[2], report->addr[3], report->addr[4], report->addr[5], report->sid);

                    esp_err_t err = esp_timer_stop(ctx->peripheral_sync_bestCentral_delay_timer);  // i forget why i put this here and its purpose , need to check later

                    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {

                        MY_LOGE(LOG_TAG_Peripheral, "Failed to stop timer: %s", esp_err_to_name(err));
                    }

                    ESP_ERROR_CHECK(esp_timer_start_once(ctx->peripheral_sync_bestCentral_delay_timer, 15000000));  // 15 sec = 15,000,000 µs
                    MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-16] PRPH_BEST_PARENT_SYNC_TIMER_STARTED" COLOR_RESET);
                }
            }
        }

        if (memcmp(&report->addr[3], &ctx->peripheral_parent_mac[3], 3) == 0) {  // check if the parent mac address is matched with the adv data of periodic sync report

            MY_LOGD(LOG_TAG_Peripheral, "parent mac address matched with ext advertiser %02X:%02X:%02X ", ctx->peripheral_parent_mac[3], ctx->peripheral_parent_mac[4], ctx->peripheral_parent_mac[5]);

            return;  // Ignore the ext adv from the parent device
        }

        if ((adv_name != NULL) && (memcmp(adv_name, "CEN_SYN_BEACON", 13) == 0)) {

            if (Cen_Ack_Mac_Extract(ctx, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len, "SYN") ==
                1) {  // extract the MAC address from the adv data from central device

                // ESP_LOG_BUFFER_HEX(LOG_TAG_Peripheral,param->ext_adv_report.params.adv_data,param->ext_adv_report.params.adv_data_len);

                MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_CYAN "CEN_SYN_BEACON" COLOR_RESET);
            }

        }

        else if ((adv_name != NULL) && (memcmp(adv_name, "CEN_ACK_BEACON", 13) == 0)) {

            if (Cen_Ack_Mac_Extract(ctx, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len, "ACK") ==
                1) {  // extract the MAC address from the adv data from central device

                MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-24] PERIPHERAL_RX_PRP_ACK_BEACON" COLOR_RESET);

                ESP_LOG_BUFFER_HEX(LOG_TAG_Peripheral, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len);

                memcpy(&ctx->peripheral_parent_mac[3], &report->addr[3], 3);  // copy the last 3 bytes of the parent MAC address from the adv data

                MY_LOGI(LOG_TAG_Peripheral, "parent (Central) mac address last 3 bytes %02X:%02X:%02X ", ctx->peripheral_parent_mac[3], ctx->peripheral_parent_mac[4], ctx->peripheral_parent_mac[5]);

                parent_code = param->ext_adv_report.params.adv_data[27];
                ctx->device.remote_code = parent_code;  // store the remote code received from the parent device

                MY_LOGD(LOG_TAG_Peripheral, "parent_code: (Central) %d", parent_code);

                ctx->device.sync = synced;

                if (ctx->peripheral_PA_Data_update_timer != NULL) {

                    esp_err_t err = esp_timer_stop(ctx->peripheral_PA_Data_update_timer);

                    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {

                        MY_LOGE(LOG_TAG_Peripheral, "Failed to stop peripheral_PA_Data_update_timer: %s", esp_err_to_name(err));
                    }

                    ESP_ERROR_CHECK(esp_timer_start_periodic(ctx->peripheral_PA_Data_update_timer, 15000000));

                    MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-35] peripheral_PA_Data_update_timer started for 15 seconds" COLOR_RESET);
                }

                update_role_and_type(ctx, EVENT_ACK_RECEIVED, parent_code);

                // update_shared_code_and_adv_payload(ctx, "[FLOW-38] REPEATER role code refresh");

                //   MY_LOGD(LOG_TAG_Peripheral, "New Role is  %d", ctx->my_new_role);

                MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-38] PRPH_RX_CEN_ACK_AND_ROLE_REPEATER" COLOR_RESET);

                msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
                msg.command = ADV_CMD_STOP_EXT;

                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                    MY_LOGE(LOG_TAG_Peripheral, "Failed to queue STOP_EXT");
                }

                msg.command = ADV_CMD_STOP_SCAN;  // stop scan because CEN_ACK_BEACON from the central device , first PA received at the central side
                msg.data_len = 0;

                xQueueSend(ctx->adv_queue, &msg, 0);

                if (ctx->my_new_role == ROLE_REPEATER) {  // switch to repeater role only if new role is repeater

                    update_shared_code_and_adv_payload(ctx, "SWITCHING ROLE TO REPEATER");

                    switch_role(ctx);

                    msg.command = ADV_CMD_STOP_SCAN;  // stop scan because CEN_ACK_BEACON from the central device , first PA received at the central side
                    msg.data_len = 0;
                    xQueueSend(ctx->adv_queue, &msg, 0);

                    if (ctx->utc_time_update.ble_time_update == TIME_UPDATED) {
                        central_schedule_next_discovery_window(ctx);
                    }
                }

                start_ext_adv_beacon(ctx);  // start the ext adv beacon after receiving the ACK beacon from the central device
            }
        }

        // Update best_central only if new_code is LOWER than existing

        break;
    }

    case ESP_GAP_BLE_PERIODIC_ADV_REPORT_EVT:  // C7

        xSemaphoreGive(ctx->test_sem);

        // this will reset the timer value to 40secs again on every periodic adv report received from the central/repeater device

        esp_timer_stop(ctx->peripheral_PA_timer);                      // Just in case
        esp_timer_start_once(ctx->peripheral_PA_timer, 40 * 1000000);  // 40 seconds

        MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-21] PRPH_PA_WATCHDOG_RESET" COLOR_RESET);

        if (ctx->peripheral_master_device.sync_handle == param->period_adv_report.params.sync_handle) {

            ctx->peripheral_master_device.ext_ack_pending = true;  // new data flag set

            MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_CYAN "PERIODIC_ADV_REPORT_EVT.. %02X:%02X:%02X  -->sync handle is 0x%x" COLOR_RESET,

                    ctx->peripheral_master_device.parent_last3_mac[0], ctx->peripheral_master_device.parent_last3_mac[1], ctx->peripheral_master_device.parent_last3_mac[2],
                    param->period_adv_report.params.sync_handle);

            ESP_LOG_BUFFER_HEX(LOG_TAG_Peripheral, param->period_adv_report.params.data, param->period_adv_report.params.data_length);

            if (param->period_adv_report.params.data_length > 28) {

                uint8_t rx_counter = param->period_adv_report.params.data[28];

                MY_LOGD(LOG_TAG_Peripheral, "RX_PARENT_PA_COUNTER: sync_handle=0x%x counter=%c hex=%02X", param->period_adv_report.params.sync_handle, rx_counter, rx_counter);
            }
        }

        if (ctx->peripheral_master_device.Periodic_Adv_Sync_Estab == true) {  // if periodic adv sync established flag is true then notify the scanning task to send the ack or sync data

            ctx->peripheral_master_device.Periodic_Adv_Sync_Estab = false;  // reset the flag

            MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-20] PRPH_FIRST_PARENT_PA_REPORT_RX" COLOR_RESET);

            ctx->peripheral_scanning_event = Periodic_PDU_ACK;  // Create a scanning event for periodic PDU ACK

            xQueueSend(ctx->peripheral_scanning_queue, &ctx->peripheral_scanning_event, portMAX_DELAY);  // Send the event to the scanning queue
        }

        break;  // code added , may be last time break was missing , it could be the isse

    default:
        break;
    }
}

static void cleanup_task(void *arg) {

    ble_node_context_t *ctx = (ble_node_context_t *)arg;
    adv_msg_t msg = {0};

    for (;;) {

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait for a notification to perform cleanup

        if (ctx->cleanup_type == cleanup_PA_timeout && ctx->peripheral_master_device.sync_handle != 0xFFFF && ctx->my_current_role != ROLE_REPEATER) {

            FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_sync_terminate(ctx->peripheral_master_device.sync_handle), ctx->test_sem);

            /*     ctx->adv_message.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
               ctx->adv_message.command = ADV_CMD_TERMINATE_SYNC;               // need to implement this!

               xQueueSend(ctx->adv_queue, &ctx->adv_message, 0);  */

            msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
            msg.command = ADV_CMD_TERMINATE_SYNC;  // need to implement this!

            xQueueSend(ctx->adv_queue, &msg, 0);

            MY_LOGD(LOG_TAG_Peripheral, "Cleanup Task: Handling PA timeout cleanup");

        }

        else if (ctx->cleanup_type == cleanup_SYNC_lost) {

            MY_LOGD(LOG_TAG_Peripheral, "Cleanup Task: Handling SYNC lost cleanup");
        }

        ctx->peripheral_master_device.is_syncing = false;
        ctx->peripheral_master_device.is_synced = false;
        ctx->peripheral_master_device.sync_handle = 0xFFFF;
        ctx->repeater_role_demote.central_present = false;  // Mark central as not present

        memset(ctx->peripheral_master_device.parent_last3_mac, 0, 3);  //  reset the last 3 nibbles because now repeater centRAL LOST , now want to sync with anonther avaiable central
        ctx->best_central.is_valid = false;                            // Reset after using
        ctx->best_central.parent_code = 255;
        memset(ctx->peripheral_parent_mac, 0, sizeof(ctx->peripheral_parent_mac));

        ctx->is_cleanup_started = false;   // reset the cleanup started flag
        ctx->cleanup_type = cleanup_none;  // reset the cleanup type

        MY_LOGD(LOG_TAG_Peripheral, "Cleanup Task: Reset peripheral master device and best central info");
    }
}

static void start_ext_adv(ble_node_context_t *ctx) {
    adv_msg_t msg = {0};

    if (ctx->peripheral_ext_adv_started == false) {

        ctx->peripheral_ack_type = ACK_Type_NONE;  // Reset the ACK type to NONE

        msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
        msg.command = ADV_CMD_UPDATE_EXT_DATA;  // ext data Raw ...

        memcpy(msg.data, &ctx->raw_ext_adv_data_2m[0], sizeof(ctx->raw_ext_adv_data_2m));
        msg.data_len = sizeof(ctx->raw_ext_adv_data_2m);

        xQueueSend(ctx->adv_queue, &msg, 0);

        msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
        msg.command = ADV_CMD_START_EXT;  // ext adv start
        xQueueSend(ctx->adv_queue, &msg, 0);

        MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-02] PRPH_NORMAL_EXT_ADV_START_REQUEST" COLOR_RESET);

        ctx->peripheral_auto_stop_ext_adv = false;  // disable the timer for auto stop
        ctx->peripheral_ext_adv_started = true;     // Set the flag to true to indicate that extended advertising has started
    }
}
static void peripheral_ack_sync_ext_adv_task(void *arg) {  // peripheral_ack_sync_ext_adv_task to facilitate the Gap event using Queue to send ack/sync & ext adv for sync establish

    uint32_t notification_value = 0;

    int position = 28;  // offset to add MAC address bytes

    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    adv_msg_t msg = {0};

    for (;;) {

        if (xQueueReceive(ctx->peripheral_scanning_queue, &ctx->peripheral_scanning_event, portMAX_DELAY) == pdTRUE) {  // Wait for an event from the scanning queue
                                                                                                                        // Stop scanning when notified
            switch (ctx->peripheral_scanning_event) {

            case Periodic_PDU_SYN:  // if sync notification received

                MY_LOGD(LOG_TAG_Peripheral, "Sync Established ....(peripheral_ack_sync_ext_adv_task)");

                ctx->peripheral_ack_type = ACK_Type_SYNC;  // not used currently... have to look later
                memcpy(&ctx->ack_data[8], "PRP", 3);
                memcpy(&ctx->ack_data[12], "SYN", 3);

                position = 28;  // update position to add MAC address bytes

                if (ctx->peripheral_master_device.sync_ack_pending) {  // if new data flag is set then update the last 3 bytes of MAC address

                    memcpy(&ctx->ack_data[position], ctx->peripheral_master_device.parent_last3_mac, 3);
                    ctx->peripheral_master_device.sync_ack_pending = false;
                }

                break;

            case Periodic_PDU_ACK:  // if periodic ack notification received

                ctx->peripheral_ack_type = ACK_Type_ACK;  // Set the ACK type to ACK
                memcpy(&ctx->ack_data[8], "PRP", 3);
                memcpy(&ctx->ack_data[12], "ACK", 3);
                memcpy(&ctx->ack_data[23], "CODE", 4);

                memcpy(&ctx->ack_data[27], get_shared_code(ctx), 1);  // copy the code to ack data
                                                                      //  memcpy(&ack_data[36],&mac_addr[3],3);           // parent MAC address last 3 bytes copied to ack data

                MY_LOGD(LOG_TAG_Peripheral, "(Periodic_PDU_ACK)shared Code is %02x", *get_shared_code(ctx));

                position = 28;  // update position to add MAC address bytes

                if (ctx->peripheral_master_device.ext_ack_pending) {  //} && tracked_devices[i].last_seen_time != 0  && (current_time - tracked_devices[i].last_seen_time)<TIMEOUT_PERIOD_MS){ // if new
                                                                      //data flag is set then update the last 3 bytes of MAC address

                    memcpy(&ctx->ack_data[position], ctx->peripheral_master_device.parent_last3_mac, 3);
                    ctx->peripheral_master_device.ext_ack_pending = false;

                    ack_ready = true;
                }

                break;

            default:

                break;
            }

            if (ack_ready) {

                ack_ready = false;

                MY_LOGD(LOG_TAG_Peripheral, "ack ready.... ");

                /*   if(ctx->is_repeater_role_demoted==true){
                  ctx->adv_status.ext_adv_started=true;
                  MY_LOGE(LOG_TAG_Peripheral, "ack ready.... ext adv started true ");
                  }
                   */

                msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
                msg.command = ADV_CMD_STOP_EXT;  // ext adv stop ...
                xQueueSend(ctx->adv_queue, &msg, 0);

                msg.command = ADV_CMD_UPDATE_EXT_DATA;  // ext adv raw / ack ...
                memcpy(msg.data, ctx->ack_data, sizeof(ctx->ack_data));
                msg.data_len = sizeof(ctx->ack_data);

                xQueueSend(ctx->adv_queue, &msg, 0);  // but in actual have to configure the ack data

                // memset(&msg, 0, sizeof(msg));
                msg.command = ADV_CMD_START_EXT;  // ext adv start ....
                xQueueSend(ctx->adv_queue, &msg, 0);

                MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-23] PRPH_PRP_ACK_BEACON_STARTED" COLOR_RESET);
                //  memset(&msg, 0, sizeof(msg));
                msg.command = ADV_CMD_STOP_EXT;  // ext adv stop  ....
                xQueueSend(ctx->adv_queue, &msg, 0);

                // ESP_LOG_BUFFER_HEX(LOG_TAG_Peripheral, ctx->ack_data,ctx->adv_message.data_len);

                if (ctx->peripheral_ack_type ==
                    ACK_Type_ACK) {  // it will resume the ext adv after confiming the ack type ack  means one full ack cycle completed or u can say one sided sync established
                    start_ext_adv(ctx);
                }
            }
        }
    }
}
/*
 * @brief Initializes and starts the BLE peripheral mode.
 *
 * This function sets up extended advertising, periodic advertising parameters,
 * creates the scanning task, and manages the periodic advertising data and events
 * for the BLE peripheral role. It handles both standard and enhanced periodic advertising.
 */

void start_peripheral_mode(ble_node_context_t *ctx) {

    adv_msg_t msg = {0};

    ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL] = false;
    ctx->adv_status.ext_adv_data_raw[EXT_ADV_HANDLE_PERIPHERAL] = false;
    ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_PERIPHERAL] = false;

    ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = false;
    ctx->adv_status.ext_adv_data_raw[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = false;
    ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = false;

    // memset(ctx->adv_status.periodic_adv_started, 0, sizeof(ctx->adv_status.periodic_adv_started));

    ctx->adv_status.ext_adv_beacon_data_raw[EXT_ADV_HANDLE_PERIPHERAL] = false;
    ctx->adv_status.ext_adv_beacon_set_params[EXT_ADV_HANDLE_PERIPHERAL] = false;
    ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL] = false;

    ctx->adv_status.periodic_adv_data_raw[EXT_ADV_HANDLE_PERIPHERAL] = false;
    ctx->adv_status.periodic_adv_set_params[EXT_ADV_HANDLE_PERIPHERAL] = false;
    ctx->adv_status.periodic_adv_started[EXT_ADV_HANDLE_PERIPHERAL] = false;

    ctx->adv_status.periodic_adv_data_raw[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = false;
    ctx->adv_status.periodic_adv_set_params[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = false;
    ctx->adv_status.periodic_adv_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = false;

    ctx->adv_status.ext_scan_started = false;
    ctx->adv_status.ext_scan_params_set = false;
    ////ctx->adv_wait.cmd_result=false;
    ctx->peripheral_master_device.is_syncing = false;
    ctx->peripheral_master_device.is_synced = false;
    ctx->peripheral_master_device.Periodic_Adv_Sync_Estab = false;
    ctx->peripheral_master_device.sync_handle = 0xFFFF;
    memset(ctx->peripheral_parent_mac, 0, sizeof(ctx->peripheral_parent_mac));
    memset(ctx->peripheral_master_device.addr, 0, 6);
    // memset(ctx->peripheral_master_device.last_3_nibbles_Cen_to_Prph,0,3);
    memset(ctx->peripheral_master_device.parent_last3_mac, 0, 3);

    // 8) RESET ALL PERIPHERAL STATE

    ctx->peripheral_auto_stop_ext_adv = false;  // control whether timer should be used
    ctx->peripheral_ext_adv_started = false;
    ctx->peripheral_scanning_event = 0;
    ctx->peripheral_ack_type = ACK_Type_NONE;

    ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
    ctx->adv_message.command = ADV_CMD_NONE;
    memset(ctx->adv_message.data, 0, sizeof(ctx->adv_message.data));
    ctx->adv_message.data_len = 0;
    ctx->adv_message.term_sync = 0;
    ctx->SID = 0;

    ble_get_mac_address(ctx->mac_addr);

    ctx->SID = (ctx->mac_addr[5] ^ ctx->mac_addr[4] ^ ctx->mac_addr[3] ^ ctx->mac_addr[2] ^ ctx->mac_addr[1] ^ ctx->mac_addr[0]) & 0x0F;  // XOR the MAC address to get the last byte

    MY_LOGI(LOG_TAG_Common, "Generated SID for both the central & peripheral: %d", ctx->SID);  // Print the generated SID

    ext_adv_params_2M_Peripheral.sid = ctx->SID;  // Set the SID in the extended advertising parameters

    init_sync_bestCentral_timer(ctx);  // Initialize the sync delay timer
    Prph_init_Ext_adv_timer(ctx);      // Initialize the advertising timer
    Prph_init_PA_timer(ctx);           // Initialize the periodic advertising timer for termination of PA sync if no PA received from central/repeater
    Peripheral_init_PA_timer(ctx);     // Initialize the periodic advertising timer for update of PA data and counter value

    Peripheral_One_Second_timer(ctx);  // Initialize the one-second timer for periodic tasks

    ctx->my_current_role = ROLE_PERIPHERAL;  // ✅ Set current role at the start

    ctx->peripheral_scanning_queue = xQueueCreate(10, sizeof(Scanning_event_t));  // Create a queue to hold scanning events

    if (ctx->peripheral_scanning_queue == NULL) {

        MY_LOGE(LOG_TAG_Common, "adv_queue creation failed");
        return;
    }

    // 0. Peripheral Queue and Handle Set

    msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;

    // 1. Set EXT ADV params

    msg.command = ADV_CMD_SET_EXT_PARAMS;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // 2. Set EXT ADV data

    msg.command = ADV_CMD_UPDATE_EXT_DATA;

    ctx->raw_ext_adv_data_2m[33] = BLE_TIME_REQUEST_MESSAGE;  // set the message type to time request for peripheral role
    ctx->raw_ext_adv_data_2m[34] = TIME_NONE;                 // set the time status to none for peripheral role
    ctx->raw_ext_adv_data_2m[35] = 0;                         // set the time secs to 0 for peripheral role

    ctx->utc_time_update.ble_time_update = TIME_NONE;  // set the time status to none for peripheral role , for internal use only

    memcpy(msg.data, ctx->raw_ext_adv_data_2m, sizeof(ctx->raw_ext_adv_data_2m));

    msg.data_len = sizeof(ctx->raw_ext_adv_data_2m);
    xQueueSend(ctx->adv_queue, &msg, 0);

    // 3. Start EXT ADV

    msg.command = ADV_CMD_START_EXT;
    xQueueSend(ctx->adv_queue, &msg, 0);

    ctx->peripheral_auto_stop_ext_adv = true;  // control whether timer should be used

    // 4. Set PA params

    memset(&msg, 0, sizeof(msg));
    msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;  // IMPORTANT: PA handle 1
    msg.command = ADV_CMD_SET_PA_PARAMS;
    xQueueSend(ctx->adv_queue, &msg, 0);

    esp_wifi_stop();  // stop the wifi because consuming extra 64mA , and in case of peripheral its not required , later on we will look into it that how to enable wifi via timers

    xTaskCreate(peripheral_ack_sync_ext_adv_task, "peripheral_ack_sync_ext_adv_task", 4096, ctx, 9,
                &ctx->peripheral_ack_sync_ext_advTaskHandle);                           // Notify the peripheral_ack_sync_ext_adv_task(ack ,sync ,adv_data[] to send the prph/repeater )
    xTaskCreate(cleanup_task, "CleanupTask", 4096, ctx, 5, &ctx->cleanup_task_handle);  // Create cleanup task
    xTaskCreate(periodic_adv_update_task, "periodic_adv_update_task", 4096, ctx, 7, &ctx->peripheral_periodic_adv_updateTaskHandle);
// peripheral role
#if (CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH)
    // set periodic adv raw data

    // 5. Set PA data
    memset(&msg, 0, sizeof(msg));
    msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;  // IMPORTANT: PA handle 1

    msg.command = ADV_CMD_UPDATE_PA_DATA;  // PA raw data ...

    memcpy(msg.data, ctx->periodic_adv_raw_data, ctx->periodic_adv_raw_data_len);
    msg.data_len = ctx->periodic_adv_raw_data_len;

    xQueueSend(ctx->adv_queue, &msg, 0);

    // 6. PA start

    memset(&msg, 0, sizeof(msg));
    msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;  // IMPORTANT: PA handle 1
    msg.command = ADV_CMD_START_PA;              // Start PA ...
    xQueueSend(ctx->adv_queue, &msg, 0);

    //    xTaskCreate(adv_command_executor_task, "adv_command_executor_task", 4096, ctx, 8, &ctx->dv_command_executortask_handle);

    msg.command = ADV_CMD_SET_SCAN;
    xQueueSend(ctx->adv_queue, &msg, 0);

    msg.command = ADV_CMD_START_SCAN;  // Start Scan ...
    xQueueSend(ctx->adv_queue, &msg, 0);

#else
    // set periodic adv raw data
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    FUNC_SEND_WAIT_SEM(esp_ble_gap_config_periodic_adv_data_raw(EXT_ADV_HANDLE, sizeof(periodic_adv_raw_data), &periodic_adv_raw_data[0]), ctx->test_sem);
    FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_start(EXT_ADV_HANDLE), test_sem);

#endif
}

void stop_peripheral_mode(ble_node_context_t *ctx) {

    adv_msg_t msg = {0};

    // 1) STOP SCAN

    msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;  // EXT_ADV_HANDLE_PERIPHERAL

    // 1) STOP SCAN

    msg.command = ADV_CMD_STOP_SCAN;

    xQueueSend(ctx->adv_queue, &msg, 0);

    // 2) TERMINATE PA
    uint16_t sh = ctx->peripheral_master_device.sync_handle;
    // if (ctx->peripheral_master_device.is_synced && ctx->peripheral_master_device.sync_handle != 0xFFFF){
    if (sh != 0xFFFF && sh != 0x0000) {
        // FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_sync_terminate(ctx->peripheral_master_device.sync_handle),ctx->test_sem);

        msg.handle_ext = EXT_ADV_HANDLE_PERIPHERAL;
        msg.term_sync = sh;
        msg.command = ADV_CMD_TERMINATE_SYNC;
        xQueueSend(ctx->adv_queue, &msg, 0);
        // ok &=wait_for_adv_cmd(ctx, ADV_CMD_TERMINATE_SYNC);

        ctx->peripheral_master_device.sync_handle = 0xFFFF;
        ctx->peripheral_master_device.is_synced = false;
        ctx->peripheral_master_device.is_syncing = false;
    }

    // 3) STOP PA

    msg.command = ADV_CMD_STOP_PA;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // 4) STOP EXT ADV

    msg.command = ADV_CMD_STOP_EXT;
    xQueueSend(ctx->adv_queue, &msg, 0);
    // ok &=wait_for_adv_cmd(ctx, ADV_CMD_STOP_EXT);

    // 5) REMOVE EXT ADV PARAMS

    msg.command = ADV_CMD_REMOVE_EXT_PARAMS;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // 6) NOW delete tasks / queues safely
    if (ctx->peripheral_ack_sync_ext_advTaskHandle != NULL) {

        vTaskDelete(ctx->peripheral_ack_sync_ext_advTaskHandle);
        ctx->peripheral_ack_sync_ext_advTaskHandle = NULL;
    }

    // 7) DELETE TIMERS
    if (ctx->peripheral_sync_bestCentral_delay_timer != NULL) {

        esp_err_t err = esp_timer_stop(ctx->peripheral_sync_bestCentral_delay_timer);
        esp_timer_delete(ctx->peripheral_sync_bestCentral_delay_timer);
        ctx->peripheral_sync_bestCentral_delay_timer = NULL;

        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {

            MY_LOGE("Timer", "Failed to stop timer: %s", esp_err_to_name(err));
        }
    }

    if (ctx->peripheral_Ext_adv_timer != NULL) {

        esp_err_t err = esp_timer_stop(ctx->peripheral_Ext_adv_timer);
        esp_timer_delete(ctx->peripheral_Ext_adv_timer);
        ctx->peripheral_Ext_adv_timer = NULL;
    }

    if (ctx->peripheral_PA_timer != NULL) {

        esp_err_t err = esp_timer_stop(ctx->peripheral_PA_timer);
        esp_timer_delete(ctx->peripheral_PA_timer);
        ctx->peripheral_PA_timer = NULL;
    }

    if (ctx->peripheral_PA_Data_update_timer != NULL) {

        esp_err_t err = esp_timer_stop(ctx->peripheral_PA_Data_update_timer);
        esp_timer_delete(ctx->peripheral_PA_Data_update_timer);
        ctx->peripheral_PA_Data_update_timer = NULL;
    }
    // 7) DELETE QUEUES
    if (ctx->peripheral_scanning_queue) {

        vQueueDelete(ctx->peripheral_scanning_queue);
        ctx->peripheral_scanning_queue = NULL;
    }

    if (ctx->adv_queue) {

        xQueueReset(ctx->adv_queue);
    }

    if (ctx->peripheral_periodic_adv_updateTaskHandle) {

        vTaskDelete(ctx->peripheral_periodic_adv_updateTaskHandle);
        ctx->peripheral_periodic_adv_updateTaskHandle = NULL;
    }
    // 8) RESET ALL PERIPHERAL STATE

    ctx->peripheral_auto_stop_ext_adv = false;  // control whether timer should be used
    ctx->peripheral_ext_adv_started = false;
    ctx->peripheral_scanning_event = 0;
    ctx->peripheral_ack_type = ACK_Type_NONE;

    memset(ctx->peripheral_master_device.addr, 0, 6);
    // memset(ctx->peripheral_master_device.last_3_nibbles_Cen_to_Prph,0,3);
    memset(ctx->peripheral_master_device.parent_last3_mac, 0, 3);
    ctx->peripheral_master_device.addr_type = 0xFF;
    ctx->peripheral_master_device.sid = 0xFF;
    ctx->peripheral_master_device.sync_handle = 0xFFFF;
    ctx->peripheral_master_device.is_synced = false;
    ctx->peripheral_master_device.is_syncing = false;
    ctx->peripheral_master_device.in_use = false;
    ctx->peripheral_master_device.is_queued = false;
    ctx->peripheral_master_device.ext_ack_pending = false;
    ctx->peripheral_master_device.sync_ack_pending = false;
    ctx->peripheral_master_device.sync_stage = STAGE_IDLE;
    ctx->peripheral_master_device.Periodic_Adv_Sync_Estab = false;
    // ctx->peripheral_master_device.sync_handle = 0xFFFF;
    ble_node_ctx.adv_status.node_scan_active =
        false;  // Because scanning was not starting at central role , when we swtich from prph to central , it only starts when we reset the controller , this flag was needed because when swtich from
                // prph to repeater it starts scanning again so we used this flag to stop scanning again in case of repeater

    ctx->best_central.parent_code = 255;  // Initialize to max value
    ctx->best_central.is_valid = false;
    memset(ctx->peripheral_parent_mac, 0, sizeof(ctx->peripheral_parent_mac));

    ctx->shared_code = 0;
}
