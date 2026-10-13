// TODO: Replace global 'i' with ctx-based index when role switching is added
#include "ble_comm_struct.h"
#include "ble_node_context.h"
#include "ble_sync_utils.h"
#include "central_mode.h"
#include "common_mode.h"
#include "log.h"
#include "mqtt_module.h"
#include "sensor_module.h"
#include "time_sync.h"


#include <sys/time.h>
#include <time.h>

#define PA_ACK_START_INDEX 12
#define PA_ACK_END_INDEX 27

log_level_t current_log_level = Print_All_log;  // Print_Selected_Log;// Print_All_log; // Default log level is set to Print_Error

int i = 0;
static uint8_t device_code;

#define MAX_SYNC_QUEUE 10

esp_ble_gap_ext_adv_params_t ext_adv_params_2M_Central = {

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

esp_ble_gap_ext_adv_params_t ext_adv_params_beacon_Central = {

    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
    .interval_min = 36000,  // 36000 * 0.625 ms = 22 500 ms
    .interval_max = 36000,  // 36000 * 0.625 ms = 22 500 ms

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

esp_ble_gap_periodic_adv_params_t periodic_adv_params_central = {
    // periodic adv params
    .interval_min = 18000,  // 18000 → 18000 × 1.25 ms = 22 500 ms
    .interval_max = 18000,
    .properties = 0,
};

static void init_periodic_devices(void) {  // initialize periodic devices

    memset(ble_node_ctx.central_tracked_devices, 0, sizeof(ble_node_ctx.central_tracked_devices));  // initialize the periodic device info structure array to 0);
}

int start_central_ext_adv_and_pa(ble_node_context_t *ctx) {
    adv_msg_t msg = {0};

    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;

    update_shared_code_and_adv_payload(ctx, " CENTRAL_START_EXT_ADV_AND_PA");

    time_t now;
    struct tm utc_time;

    time(&now);  // Get the current time in seconds since the epoch
    gmtime_r(&now, &utc_time);

    ctx->utc_time_update.utc_seconds = (uint8_t)utc_time.tm_sec;  // only seconds are required to send to peripheral
                                                                  //    ctx->raw_ext_adv_data_2m[EXT_ADV_UTC_SECONDS_INDEX] = ctx->utc_time_update.utc_seconds;      // set the UTC seconds

    ctx->raw_ext_adv_data_2m[EXT_ADV_MESSAGE_TYPE_INDEX] = BLE_TIME_RESPONSE_MESSAGE;        // set the message type to time response
    ctx->raw_ext_adv_data_2m[EXT_ADV_TIME_STATUS_INDEX] = TIME_UPDATED;                      // set the time status to updated
    ctx->raw_ext_adv_data_2m[EXT_ADV_UTC_SECONDS_INDEX] = ctx->utc_time_update.utc_seconds;  // set the UTC seconds

    ESP_LOGE(LOG_TAG_NTP, "UTC fresh timestamp latched before queuing adv: %04d-%02d-%02d %02d:%02d:%02d", utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday, utc_time.tm_hour,
             utc_time.tm_min, utc_time.tm_sec);

    msg.command = ADV_CMD_UPDATE_EXT_DATA;
    memcpy(msg.data, &ctx->raw_ext_adv_data_2m[0], sizeof(ctx->raw_ext_adv_data_2m));
    msg.data_len = sizeof(ctx->raw_ext_adv_data_2m);

    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
        MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL UPDATE_EXT_DATA");
        return 0;
    }

    // 3) START EXTENDED ADV
    msg.command = ADV_CMD_START_EXT;
    msg.data_len = 0;

    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
        MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL START_EXT");
        return 0;
    }

    // 4) SET PA PARAMS
    msg.command = ADV_CMD_SET_PA_PARAMS;
    msg.data_len = 0;

    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
        MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL SET_PA_PARAMS");
        return 0;
    }

    // 5) SET PA DATA
    msg.command = ADV_CMD_UPDATE_PA_DATA;

    memcpy(msg.data, &ctx->periodic_adv_raw_data[0], sizeof(ctx->periodic_adv_raw_data));

    msg.data_len = sizeof(ctx->periodic_adv_raw_data);

    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
        MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL UPDATE_PA_DATA");
        return 0;
    }

    // 6) START PA
    msg.command = ADV_CMD_START_PA;
    msg.data_len = 0;

    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
        MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL START_PA");
        return 0;
    }

    return 1;
}

static periodic_device_info_t *find_or_add_periodic_device(ble_node_context_t *ctx, const esp_bd_addr_t addr, uint8_t addr_type, uint8_t sid) {

    // Check if device (MAC + SID) already exists
    for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

        if (ctx->central_tracked_devices[i].in_use && memcmp(ctx->central_tracked_devices[i].addr, addr, 6) == 0 && ctx->central_tracked_devices[i].sid == sid) {

            MY_LOGI(LOG_TAG_Central, "Record already present: MAC=%02X:%02X:%02X:%02X:%02X:%02X, SID=%d", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], sid);

            MY_LOGD(LOG_TAG_Central, "g periodic synced %d, syncing %d, in use %d", (int)ctx->central_tracked_devices[i].is_synced, (int)ctx->central_tracked_devices[i].is_syncing,
                    (int)ctx->central_tracked_devices[i].in_use);

            return &ctx->central_tracked_devices[i];  // if record found then return the record full structure
        }
    }

    // Add new entry
    for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

        if (!ctx->central_tracked_devices[i].in_use) {
            ctx->central_tracked_devices[i].in_use = true;
            memcpy(ctx->central_tracked_devices[i].addr, addr, 6);
            ctx->central_tracked_devices[i].addr_type = addr_type;
            ctx->central_tracked_devices[i].sid = sid;
            ctx->central_tracked_devices[i].sync_handle = 0xFFFF;
            ctx->central_tracked_devices[i].is_synced = false;
            ctx->central_tracked_devices[i].is_syncing = false;

            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-29] CENTRAL_PERIODIC_DEVICE_SLOT_UPDATED: MAC=%02X:%02X:%02X:%02X:%02X:%02X, SID=%d" COLOR_RESET, addr[0], addr[1], addr[2], addr[3],
                    addr[4], addr[5], sid);

            return &ctx->central_tracked_devices[i];
        }
    }
    return NULL;
}

int find_or_add_discovered_prph_slot(ble_node_context_t *ctx, esp_bd_addr_t addr) {
    uint8_t *last3 = &addr[3];
    int empty_slot_index = -1;
    uint8_t empty_parent_last3[3] = {0, 0, 0};

    // FILTER #1:  If this MAC is my parent MAC, ignore it to avoid loop.

    // Parent MAC filter is active only when a valid non-zero parent MAC is stored; avoids false loop filtering before this node becomes a Repeater.

    if (memcmp(&ctx->peripheral_parent_mac[3], empty_parent_last3, 3) != 0 && memcmp(last3, &ctx->peripheral_parent_mac[3], 3) == 0) {

        MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-08]  FILTER #1,Central Ignoring parent MAC to avoid loop: %02X:%02X:%02X" COLOR_RESET, last3[0], last3[1], last3[2]);

        return -1;
    }

    for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

        // FILTER #2: If device already exists, return its index. Do not check sync_stage here. The main state machine will decide what to do next.

        if (memcmp(ctx->central_tracked_devices[i].discovery_last3_mac, last3, 3) == 0) {

            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-09] FILTER #2,Device already exists at index  %d with stage %d" COLOR_RESET, i, ctx->central_tracked_devices[i].sync_stage);

            return i;
        }

        // Find first empty slot

        if ((memcmp(ctx->central_tracked_devices[i].discovery_last3_mac, "\x00\x00\x00", 3) == 0) && ctx->central_tracked_devices[i].sync_stage == STAGE_IDLE && empty_slot_index == -1) {

            empty_slot_index = i;
        }
    }

    //  Add new device

    if (empty_slot_index != -1) {

        memcpy(ctx->central_tracked_devices[empty_slot_index].discovery_last3_mac, last3, 3);  // copy last 3 bytes of MAC address into discovery_last3_mac

        ctx->central_tracked_devices[empty_slot_index].sync_stage = STAGE_IDLE;

        //  MY_LOGI(LOG_TAG_Central, "Added new device at index %d with last 3 = %02X:%02X:%02X", empty_slot_index, last3[0], last3[1], last3[2]);

        MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-07] CENTRAL_DISCOVERED_PRPH_SLOT_UPDATED" COLOR_RESET);

        ctx->ext_adv_detected = true;  // Set the flag to indicate that extended advertising has been detected

        return empty_slot_index;
    }

    return -1;
}
// STEP 2: Task that processes the queue one by one
void periodic_sync_worker_task(void *param) {  // Task to create periodic sync using the queue

    periodic_device_info_t *dev_ptr;
    ble_node_context_t *ctx = (ble_node_context_t *)param;

    while (1) {

        if (xQueueReceive(ctx->central_syncQueue, &dev_ptr, portMAX_DELAY)) {

            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-31] CENTRAL_SYNC_WORKER_DEQUEUED_DEVICE_PTR " COLOR_RESET);
            create_periodic_sync(dev_ptr);
            // ctx->central_tracked_devices[i].sync_stage = STAGE_PA_SYNCING;
            dev_ptr->sync_stage = STAGE_PA_SYNCING;
            dev_ptr->is_queued = false;               // <-- Reset the flag here
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait for sync complete
        }
    }
}

static void pa_update_timer_cb(void *arg) {  // periodic advertising timer callback

    ble_node_context_t *ctx = (ble_node_context_t *)arg;
    MY_LOGW(LOG_TAG_Central, "Periodic Advertising Timer expired → update sensor data..");

    xTaskNotify(ctx->central_periodic_adv_updateTaskHandle, START_PERIODIC_ADV, eSetBits);  // Notify the task to start periodic advertising
}

void Central_init_PA_timer(ble_node_context_t *ctx) {  // in this function we initialize the periodic advertising timer

    const esp_timer_create_args_t timer_args = {

        .callback = &pa_update_timer_cb,
        .arg = ctx,  // no argument needed
        .name = "pa_update_timer"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->central_PA_timer));
}

static void adv_timeout_cb(void *arg) {
    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    MY_LOGD(LOG_TAG_Central, "100 s elapsed → stopping advertising");
    /*
        adv_msg_t msg = {0};

        msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
        msg.command = ADV_CMD_STOP_EXT;

        if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE)
        {
            MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL STOP_EXT from adv_timeout_cb");
        } */
}

static void one_second_timer_cb(void *arg) {
    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    MY_LOGD(LOG_TAG_Central, "1 s elapsed → Print UTC time");

    time_t now;
    struct tm utc_time;

    time(&now);

    gmtime_r(&now, &utc_time);

    MY_LOGD(LOG_TAG_NTP, "UTC: %04d-%02d-%02d %02d:%02d:%02d", utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday, utc_time.tm_hour, utc_time.tm_min, utc_time.tm_sec);

    adv_msg_t msg = {0};
    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;

    // 1) STOP SCAN

    msg.command = ADV_CMD_START_SCAN;
    msg.data_len = 0;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // ESP_ERROR_CHECK(esp_timer_start_once(ctx->central_one_second_timer, 60000000)); // start one short timer again to enable/ start the scanner agaim after every 60 secs

    if (!esp_timer_is_active(ctx->central_one_second_timer)) {

        ESP_ERROR_CHECK(esp_timer_start_once(ctx->central_one_second_timer, 60000000));  // 60 seconds
    }

    if (!esp_timer_is_active(ctx->central_three_seconds_timer_scan)) {

        ESP_ERROR_CHECK(esp_timer_start_once(ctx->central_three_seconds_timer_scan, 5000000));  // 5 seconds
    }
}

static void three_seconds_timer_cb(void *arg) {
    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    if (ctx->ext_adv_detected == false) {

        adv_msg_t msg = {0};
        msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;

        // 1) STOP SCAN

        msg.command = ADV_CMD_STOP_SCAN;
        msg.data_len = 0;
        xQueueSend(ctx->adv_queue, &msg, 0);

        MY_LOGD(LOG_TAG_Central, "SCAN STOPPED after 5 seconds because no extended advertising detected from any peripheral device");
    }
}
void Central_init_Ext_adv_timer(ble_node_context_t *ctx) {

    const esp_timer_create_args_t timer_args = {

        .callback = &adv_timeout_cb,
        .arg = ctx,  // Argument passed
        .name = "Central_adv_timer"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->central_adv_timer));
}

void Central_One_Second_timer(ble_node_context_t *ctx) {

    const esp_timer_create_args_t timer_args = {

        .callback = &one_second_timer_cb,
        .arg = ctx,  // Argument passed
        .name = "One_Second_timer"

    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->central_one_second_timer));
}

void Central_Three_Seconds_timer_SCAN(ble_node_context_t *ctx) {

    const esp_timer_create_args_t timer_args = {

        .callback = &three_seconds_timer_cb,
        .arg = ctx,  // Argument passed
        .name = "Three_Seconds_timer"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ctx->central_three_seconds_timer_scan));
}

static void
start_ext_adv_beacon(ble_node_context_t *ctx) {  // this function is to start the extended advertising with the beacon data when the device is in Central role and not synced with any peripheral device

    adv_msg_t msg = {0};

    if (ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL_BEACON] == false) {

        // 1. Set EXT ADV params

        msg.handle_ext = EXT_ADV_HANDLE_CENTRAL_BEACON;
        msg.command = ADV_CMD_SET_EXT_PARAMS;

        if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

            MY_LOGE(LOG_TAG_Central, "Failed to queue beacon SET_EXT_PARAMS");
            return;
        }

        // 2. UPDATE_EXT_DATA

        if (ctx->periodic_beacon_data_len == 0 || ctx->periodic_beacon_data_len > sizeof(msg.data)) {

            MY_LOGE(LOG_TAG_Central, "Invalid beacon data length: %d", ctx->periodic_beacon_data_len);

            return;
        }

        msg.command = ADV_CMD_UPDATE_EXT_DATA;
        memset(msg.data, 0, sizeof(msg.data));
        memcpy(msg.data, &ctx->periodic_beacon_data[0], ctx->periodic_beacon_data_len);  // because we want to send the beacon messages and data will be the same as of the periodic adv data
        msg.data_len = ctx->periodic_beacon_data_len;

        if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
            MY_LOGE(LOG_TAG_Central, "Failed to queue beacon UPDATE_EXT_DATA");
            return;
        }

        // 3. Start EXT ADV beacon

        msg.command = ADV_CMD_START_EXT;

        if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
            MY_LOGE(LOG_TAG_Central, "Failed to queue beacon START_EXT");
            return;
        }

        MY_LOGI(LOG_TAG_Central, COLOR_ORANGE "[FLOW-37] CENTRAL_CEN_ACK_BEACON_STARTED" COLOR_RESET);
    }
}
void gap_event_handler_Central(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param, ble_node_context_t *ctx) {
    switch (event) {

    case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:  // 1
        xSemaphoreGive(ctx->test_sem);
        // MY_LOGV(LOG_TAG_Central, "Extended scanning params set, status %d", param->set_ext_scan_params.status);

        break;
    case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:  // 2
        xSemaphoreGive(ctx->test_sem);
        //   MY_LOGI(LOG_TAG_Central, "Extended scanning start, status %d", param->ext_scan_start.status);

        break;
    case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:  // 3
        xSemaphoreGive(ctx->test_sem);
        //  MY_LOGI(LOG_TAG_Central, "Extended scanning stop, status %d", param->ext_scan_stop.status);

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
        //  MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT, status %d, instance %d", param->ext_adv_set_rand_addr.status, param->ext_adv_set_rand_addr.instance);

        break;

    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT:  // 5
        xSemaphoreGive(ctx->test_sem);
        //  MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT, status %d, instance %d", param->ext_adv_set_params.status, param->ext_adv_set_params.instance);

        break;

    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT:  // 6
        xSemaphoreGive(ctx->test_sem);
        // MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT, status %d, instance %d", param->ext_adv_data_set.status, param->ext_adv_set_params.instance);

        break;
    case ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT:
        xSemaphoreGive(ctx->test_sem);
        //  MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT, status %d, instance %d", param->ext_adv_remove.status, param->ext_adv_remove.instance);

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
        //   MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT, status %d, instance number %d", param->ext_adv_start.status, param->ext_adv_start.instance_num);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_START_EXT) {
            if (param->ext_adv_start.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;

            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        if (param->ext_adv_start.status == ESP_BT_STATUS_SUCCESS && ctx->central_auto_stop_ext_adv) {

            ctx->central_auto_stop_ext_adv = false;  // reset the flag

            if (ctx->central_adv_timer != NULL) {  // check if timer is created

                esp_err_t err = esp_timer_stop(ctx->central_adv_timer);  // stop the timer if already running

                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    MY_LOGE(LOG_TAG_Central, "Failed to stop Central_adv_timer: %s", esp_err_to_name(err));
                }

                ESP_ERROR_CHECK(esp_timer_start_once(ctx->central_adv_timer, 100000000));  // 100 seconds
            }
        }

        break;

    case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT:  // 8
        xSemaphoreGive(ctx->test_sem);
        // MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT, status %d, instance number %d", param->ext_adv_stop.status, param->ext_adv_stop.instance_num);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_STOP_EXT) {

            if (param->ext_adv_stop.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;
            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }
        break;

    case ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT:  // P1
        xSemaphoreGive(ctx->test_sem);
        //  MY_LOGV(LOG_TAG_Central, "SP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT, status %d, instance %d", param->scan_rsp_set.status, param->scan_rsp_set.instance);

        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT:  // P2
        xSemaphoreGive(ctx->test_sem);
        //   MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT, status %d, instance %d", param->peroid_adv_set_params.status, param->peroid_adv_set_params.instance);

        break;

    case ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT:  // P3
        xSemaphoreGive(ctx->test_sem);
        //    MY_LOGV(LOG_TAG_Central, "ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT, status %d, instance %d", param->period_adv_data_set.status, param->period_adv_data_set.instance);

        break;
    case ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT:  // P4
        xSemaphoreGive(ctx->test_sem);

        MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-12] ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT, status %d, instance %d" COLOR_RESET, param->period_adv_start.status,
                param->period_adv_start.instance);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_START_PA) {
            if (param->period_adv_start.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;
            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        break;

    case ESP_GAP_BLE_PERIODIC_ADV_CREATE_SYNC_COMPLETE_EVT:  // C1
        xSemaphoreGive(ctx->test_sem);
        MY_LOGV(LOG_TAG_Central, "Periodic advertising create sync, status %d", param->period_adv_create_sync.status);

        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_CANCEL_COMPLETE_EVT:  // C2
        xSemaphoreGive(ctx->test_sem);
        MY_LOGV(LOG_TAG_Central, "Periodic advertising sync cancel, status %d", param->period_adv_sync_cancel.status);

        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_TERMINATE_COMPLETE_EVT:  // C3
        xSemaphoreGive(ctx->test_sem);

        MY_LOGD(LOG_TAG_Central, "Periodic advertising sync terminate, status %d", param->period_adv_sync_term.status);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_TERMINATE_SYNC) {
            if (param->period_adv_sync_term.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;
            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        break;

    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_LOST_EVT: {  // C4
        xSemaphoreGive(ctx->test_sem);
        uint16_t sync_handle = param->periodic_adv_sync_lost.sync_handle;  // periodic advertising sync lost event

        for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

            if (ctx->central_tracked_devices[i].sync_handle == sync_handle) {

                ctx->central_tracked_devices[i].is_synced = false;
                ctx->central_tracked_devices[i].is_syncing = false;
                ctx->central_tracked_devices[i].in_use = false;

                memset(&ctx->central_tracked_devices[i], 0, sizeof(ctx->central_tracked_devices[i]));  //
                ctx->central_tracked_devices[i].sync_handle = 0xFFFF;

                MY_LOGW(LOG_TAG_Central, "Sync Lost (Prph) %d, sync handle is 0x%x", i, ctx->central_tracked_devices[i].sync_handle);

                break;
            }
        }

        // recalculate the peripheral presence
        bool any_synced = false;
        for (int j = 0; j < CENTRAL_MAX_PERIODIC_DEVICES; j++) {
            if (ctx->central_tracked_devices[j].is_synced) {
                any_synced = true;
                break;
            }
        }

        ctx->repeater_role_demote.peripheral_present = any_synced;  // Update peripheral presence status
        break;
    }

    case ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT: {

        xSemaphoreGive(ctx->test_sem);

        MY_LOGD(LOG_TAG_Central, "PA STOP complete, status = %d", param->period_adv_stop.status);

        if (ctx->adv_wait.pending_cmd == ADV_CMD_STOP_PA) {
            if (param->period_adv_stop.status == ESP_OK)
                ctx->adv_wait.cmd_result = true;
            else
                ctx->adv_wait.cmd_result = false;

            ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
        }

        break;
    }

    case ESP_GAP_BLE_PERIODIC_ADV_SYNC_ESTAB_EVT: {  // C5

        xSemaphoreGive(ctx->test_sem);  // code added , in previous evt as pasted before dont have this line { }  , need to check
        uint8_t *addr = param->periodic_adv_sync_estab.adv_addr;
        uint16_t sync_handle = param->periodic_adv_sync_estab.sync_handle;
        uint8_t empty_array[3] = {0, 0, 0};

        adv_msg_t msg = {0};
        //  uint8_t code;

        if (param->periodic_adv_sync_estab.status == ESP_OK) {

            for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

                if (ctx->central_tracked_devices[i].in_use && memcmp(ctx->central_tracked_devices[i].addr, addr, 6) == 0 && ctx->central_tracked_devices[i].sid == param->periodic_adv_sync_estab.sid) {

                    ctx->central_tracked_devices[i].is_synced = true;
                    ctx->central_tracked_devices[i].is_syncing = false;
                    ctx->central_tracked_devices[i].sync_handle = sync_handle;
                    ctx->central_tracked_devices[i].Periodic_Adv_Sync_Estab = true;  //  its from prph ---> central

                    if (memcmp(ctx->central_tracked_devices[i].synced_peer_last3_mac, empty_array, 3) == 0) {
                        memcpy(ctx->central_tracked_devices[i].synced_peer_last3_mac, &addr[3], 3);  //  its from prph ---> central
                        ctx->central_tracked_devices[i].sync_ack_pending = true;
                    }

                    MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-33] CENTRAL_SYNC_ESTABLISHED_TO_PRPH_PA (prph->central) sid %d" COLOR_RESET, ctx->central_tracked_devices[i].sid);
                    /*
                                        ctx->device.sync = synced;     // set sync flag to true
                                        get_type(&ctx->device, &code); // update the device type and code ,because now central promotes from AO,B0,C0 to CR,BR,CR etc
                                        set_shared_code(ctx, code);    // Set the shared code
                     */

                    ctx->device.sync = synced;                                                     // set sync flag to true
                    update_shared_code_and_adv_payload(ctx, "SYNC ESTABLLISHED (prph->central)");  // update the shared code and adv payload

                    ctx->central_tracked_devices[i].sync_stage = STAGE_PA_SYNCED;

                    msg.command = ADV_CMD_STOP_SCAN;  // stop scan because PRP_ACK_BEACON from the peripheral device , first PA received at the peripheral side
                    msg.data_len = 0;

                    xQueueSend(ctx->adv_queue, &msg, 0);

                    ctx->ext_adv_detected = false;  // reset the flag to false because we have already received the extended advertising from the peripheral device and now we can stop the scanning

                    xTaskNotifyGive(ctx->central_sync_worker_TaskHandle);  // Notify the periodic sync worker task that sync is established and it can process the next device in the queue
                    xTaskNotify(ctx->central_pa_ack_syncTaskHandle, Periodic_PDU_SYN, eSetBits);  // Notify the PA ACK sync task that sync is established and it can start waiting for PA ACK from prph
                    break;
                }
            }

            bool any_synced = false;

            for (int j = 0; j < CENTRAL_MAX_PERIODIC_DEVICES; j++) {
                if (ctx->central_tracked_devices[j].is_synced) {
                    any_synced = true;
                    break;
                }
            }

            ctx->repeater_role_demote.peripheral_present = any_synced;  // Update peripheral presence status
        }

        break;
    }

    case ESP_GAP_BLE_EXT_ADV_REPORT_EVT: {  // C6   report from prph/repeater to central

        xSemaphoreGive(ctx->test_sem);

        uint8_t *adv_name = NULL;
        uint8_t adv_name_len = 0;

        esp_ble_gap_ext_adv_report_t *report = &param->ext_adv_report.params;  // its the extended advertising report event

        adv_name = esp_ble_resolve_adv_data(param->ext_adv_report.params.adv_data, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);

        MY_LOGD(LOG_TAG_Central, "Ext Adv Report: MAC=%02X:%02X:%02X:%02X:%02X:%02X, SID=%d", report->addr[0], report->addr[1], report->addr[2], report->addr[3], report->addr[4], report->addr[5],
                report->sid);

        if (ctx->my_current_role != ROLE_REPEATER) {

            MY_LOGD(LOG_TAG_Central, "adv name = %.*s", adv_name_len, adv_name ? (char *)adv_name : "N/A");
        }

        if (strncmp((char *)adv_name, "ESP_MULTI_ADV_80MS!", 19) == 0) {
            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-06] CENTRAL_RX_PRPH_NORMAL_ADV" COLOR_RESET);
            ESP_LOG_BUFFER_HEX(LOG_TAG_Central, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len);
        }

        if (report->sid > 15) {

            MY_LOGW(LOG_TAG_Central, "Ignoring SID=%d (not 0-3)", report->sid);

            return;  // last time was return
        }

        i = find_or_add_discovered_prph_slot(ctx, report->addr);  // Find or add the device slot in the periodic device info structure array

        if (i < 0) {

            MY_LOGD(LOG_TAG_Central, "No slot available for device %02X:%02X:%02X:%02X:%02X:%02X", report->addr[0], report->addr[1], report->addr[2], report->addr[3], report->addr[4],
                    report->addr[5]);
            return;  // No slot available, exit  ,
        }

        //  MY_LOGE(LOG_TAG_Central, "central_tracked_devices[i].sync_stage = %d", ctx->central_tracked_devices[i].sync_stage);

        if (report->adv_data_len > EXT_ADV_UTC_SECONDS_INDEX && report->adv_data[EXT_ADV_MESSAGE_TYPE_INDEX] == BLE_TIME_REQUEST_MESSAGE &&
            !ctx->time_request_handled) {  // means it least 35 bytes should be received in the adv data and the message type should be BLE_TIME_REQUEST_MESSAGE

            //   ctx->time_request_handled = true; // set the flag to true to avoid multiple time request handling

            ctx->utc_time_update.ble_message_type =
                (ble_message_type_t)report->adv_data[EXT_ADV_MESSAGE_TYPE_INDEX];  // store the message type received from the peripheral in the central context structure

            // 1. Refresh TIME_UPDATED + current UTC second in raw_ext_adv_data_2m
            utc_normal_message_action_from_Peripheral(ctx);

            // 2. If EXT ADV is already running, push the new payload only
            if (ctx->central_tracked_devices[i].sync_stage != STAGE_IDLE) {
                adv_msg_t msg = {0};

                msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
                msg.command = ADV_CMD_UPDATE_EXT_DATA;

                memcpy(msg.data, ctx->raw_ext_adv_data_2m, sizeof(ctx->raw_ext_adv_data_2m));

                msg.data_len = sizeof(ctx->raw_ext_adv_data_2m);

                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                    MY_LOGE(LOG_TAG_Central, "Failed to queue fresh TIME RESPONSE EXT data");
                }
            }
        }

        if (ctx->central_tracked_devices[i].sync_stage == STAGE_IDLE) {

            if (start_central_ext_adv_and_pa(ctx) == 1) {

                ctx->central_tracked_devices[i].sync_stage = STAGE_WAITING_PRPH_ACK;  // Update the sync stage to indicate that extended advertising & PA has been started for this device

                ctx->central_auto_stop_ext_adv = true;  // enable the timer for auto stop , 100 secs

                MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-10] CENTRAL_EXT_ADV_AND_PA_START_REQUEST (STAGE_IDLE)" COLOR_RESET);

            }  // start central extended advertising and periodic advertising
        }

        if ((adv_name != NULL) && (memcmp(adv_name, "PRP_SYN_BEACON", 13) == 0)) {
            // if (adv_name && adv_name_len >= 13 && memcmp(adv_name,"PRP_SYN_BEACON",13)==0){

            if (Cen_Ack_Mac_Extract(ctx, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len, "SYN") == 1) {

                // extract the MAC address from the advertising data received in central role and compare it with the stored mac address to check whether the ACK/SYN is from the synced peripheral or
                // not , return true if match found otherwise false

                //   ESP_LOG_BUFFER_HEX(LOG_TAG_Central, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len);

                MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_CYAN "PRP_SYN_BEACON" COLOR_RESET);
            }
        }

        else if ((adv_name != NULL) && (memcmp(adv_name, "PRP_ACK_BEACON", 13) == 0)) {

            if (Cen_Ack_Mac_Extract(ctx, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len, "ACK") == 1) {

                // extract the MAC address from the advertising data received in central role and compare it with the stored mac address to check whether the ACK/SYN is from the synced peripheral or
                // not , return true if match found otherwise false

                MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-24] CENTRAL_RX_PRP_ACK_BEACON" COLOR_RESET);
                ESP_LOG_BUFFER_HEX(LOG_TAG_Central, param->ext_adv_report.params.adv_data, param->ext_adv_report.params.adv_data_len);

                memcpy(&ctx->peripheral_child_mac[3], &report->addr[3], 3);  // copy the last 3 bytes of the parent MAC address from the adv data

                MY_LOGD(LOG_TAG_Central, COLOR_BRIGHT_BLUE "(child/Prph) mac address last 3 bytes %02X:%02X:%02X " COLOR_RESET, ctx->peripheral_child_mac[3], ctx->peripheral_child_mac[4],
                        ctx->peripheral_child_mac[5]);

                if (memcmp(ctx->central_tracked_devices[i].discovery_last3_mac, &report->addr[3], 3) == 0 && ctx->central_tracked_devices[i].sync_stage == STAGE_WAITING_PRPH_ACK) {

                    // check if the last 3 bytes of the central mac address is same as the last 3 bytes of the parent mac address extracted from the ACK adv data

                    MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-27] CENTRAL_STAGE_PRPH_ACK_RECEIVED " COLOR_RESET);

                    adv_msg_t msg = {0};

                    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
                    msg.command = ADV_CMD_STOP_EXT;

                    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
                        MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL STOP_EXT after PRP_ACK_BEACON");
                    }

                    MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-28] CENTRAL_NORMAL_EXT_ADV_STOP_REQUEST" COLOR_RESET);

                    ctx->central_tracked_devices[i].sync_stage = STAGE_PRPH_ACK_RECEIVED;  // Update the sync stage to indicate that the central has received the ACK from the parent/ peripheral

                    // Stage-1 is complete; PRPH has accepted Central side,PA completed C-->P, PRP_ACK_BEACON via Ext adv from Prph ---> central

                    periodic_device_info_t *device = find_or_add_periodic_device(ctx, report->addr, report->addr_type,
                                                                                 report->sid);  // record return from find or add periodic device function ,store the address of device structure

                    if (device && !device->is_syncing && !device->is_synced && !device->is_queued) {  // added this && device->in_use==true   && device->in_use==true

                        if (xQueueSend(ctx->central_syncQueue, &device, 0) == pdTRUE) {

                            device->is_queued = true;
                            ctx->central_tracked_devices[i].sync_stage = STAGE_PA_SYNC_QUEUED;
                            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-30] CENTRAL_PRPH_PA_SYNC_QUEUED" COLOR_RESET);

                        }

                        else {

                            MY_LOGE(LOG_TAG_Central, "Queue FULL! Failed to enqueue SID=%d", report->sid);
                        }
                    }
                }
            }
        }

        break;
    }

    case ESP_GAP_BLE_PERIODIC_ADV_REPORT_EVT: {  // C7

        xSemaphoreGive(ctx->test_sem);
        int i = 0;  // Initialize i here to avoid scope issues

        for (i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {  // for mac address and sync handle mapping

            if (ctx->central_tracked_devices[i].sync_handle == param->period_adv_report.params.sync_handle) {

                ctx->central_tracked_devices[i].ext_ack_pending = true;  //

                ctx->central_tracked_devices[i].pa_ack_pending = true;  //
                ctx->time_request_handled = false;                      // reset the flag to false to allow the time request handling again

                MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_CYAN "PERIODIC_ADV_REPORT_EVT.. %02X:%02X:%02X  -->sync handle is 0x%x" COLOR_RESET,

                        ctx->central_tracked_devices[i].synced_peer_last3_mac[0], ctx->central_tracked_devices[i].synced_peer_last3_mac[1], ctx->central_tracked_devices[i].synced_peer_last3_mac[2],
                        param->period_adv_report.params.sync_handle);

                //  MY_LOGI(LOG_TAG_Central, "Periodic Adv Data from Prph is %s", param->period_adv_report.params.data);

                ESP_LOG_BUFFER_HEX(LOG_TAG_Central, param->period_adv_report.params.data, param->period_adv_report.params.data_length);

                if (param->period_adv_report.params.data_length > 28) {

                    uint8_t rx_counter = param->period_adv_report.params.data[28];

                    MY_LOGD(LOG_TAG_Central, "RX_PRPH_PA_COUNTER: sync_handle=0x%x counter=%c hex=%02X", param->period_adv_report.params.sync_handle, rx_counter, rx_counter);
                }

                break;
            }
        }

        if (i == CENTRAL_MAX_PERIODIC_DEVICES) {

            break;
        }
        if (ctx->central_tracked_devices[i].Periodic_Adv_Sync_Estab ==
            true) {  // it will exectute only once after the sync is established and first periodic adv report is received from the peripheral device

            ctx->central_tracked_devices[i].Periodic_Adv_Sync_Estab = false;  // reset the flag after first periodic adv report received after sync established

            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-34] CENTRAL_FIRST_PRPH_PA_REPORT_RX" COLOR_RESET);

            if (ctx->central_PA_timer != NULL) {
                esp_err_t err = esp_timer_stop(ctx->central_PA_timer);

                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    MY_LOGE(LOG_TAG_Central, "Failed to stop central_PA_timer: %s", esp_err_to_name(err));
                }

                ESP_ERROR_CHECK(esp_timer_start_periodic(ctx->central_PA_timer, 22500000));

                MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-35] CENTRAL_PA_UPDATE_TIMER_STARTED_22_5S" COLOR_RESET);
            }

            xTaskNotify(ctx->central_pa_ack_syncTaskHandle, Periodic_PDU_ACK,
                        eSetBits);  // Notify the central_pa_ack_sync_task(ack ,sync ,adv_data[] to send the prph/repeater )that sync is established

            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-36] CENTRAL_ACK_TASK_NOTIFIED" COLOR_RESET);

            start_ext_adv_beacon(ctx);  //
        }

        break;  // code added , may be last time break was missing , it could be the isse
    }

    default:
        break;
    }
}

static void periodic_adv_update_task(void *arg) {  // task to facilitate the Gap event using Notify to send start periodic adv with counter value in the adv data

    uint32_t notification_value = 0;
    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    static float Pre_AverageX = 0.0f, Pre_AverageY = 0.0f;
    float Curr_AverageX = 0.0f, Curr_AverageY = 0.0f;
    static bool average_update_flag = false;

    static int counter = 0;

    for (;;) {

        xTaskNotifyWait(0, ULONG_MAX, &notification_value, portMAX_DELAY);  // ULONG_MAX set bits on exit

        if (notification_value & START_PERIODIC_ADV) {

            /* ---------------- Update counter safely ---------------- */

            int position = 12;

            memset(&ctx->periodic_adv_raw_data[PA_ACK_START_INDEX], 0, PA_ACK_END_INDEX - PA_ACK_START_INDEX + 1);  // Clear the periodic adv data from index 12 to 27 before updating with new data

            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-40] CENTRAL_PERIODIC_ADV_UPDATE_TASK_STARTED" COLOR_RESET);
            for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

                if (ctx->central_tracked_devices[i].pa_ack_pending) {

                    if (position + 3 < ctx->periodic_adv_raw_data_len) {  // if there is enough space in the periodic adv data to add the new device MAC address

                        memcpy(&ctx->periodic_adv_raw_data[position], &ctx->central_tracked_devices[i].synced_peer_last3_mac[0],
                               3);  // copy the last 3 bytes of the synced peripheral mac address to the periodic adv data

                    }

                    else {

                        MY_LOGE(LOG_TAG_Central, "Not enough space in periodic adv data to add device %02X:%02X:%02X", ctx->central_tracked_devices[i].synced_peer_last3_mac[0],
                                ctx->central_tracked_devices[i].synced_peer_last3_mac[1], ctx->central_tracked_devices[i].synced_peer_last3_mac[2]);
                    }
                }

                position += 3;                                           // Move to the next position for the next device
                ctx->central_tracked_devices[i].pa_ack_pending = false;  // reset the flag after processing the new data
            }

            MY_LOGD(LOG_TAG_Central, "periodic adv raw data len: %d, periodic beacon data len: %d", ctx->periodic_adv_raw_data_len, ctx->periodic_beacon_data_len);

            if (ctx->periodic_adv_raw_data_len > 28) {

                ctx->periodic_adv_raw_data[28] = counter + 48;
            }

            if (ctx->periodic_beacon_data_len > 26) {

                ctx->periodic_beacon_data[26] = counter + 48;
            }

            counter++;

            if (counter > 9) {

                counter = 0;
            }
            if (ctx->my_new_role == ROLE_CENTRAL) {

                Read_Sensor_Calc_Average(&Curr_AverageX, &Curr_AverageY);  // read the sensor data and calculate the average values for X and Y axis, return the pointer to the average values

                // Curr_AverageX= *(Average_Values);
                // Curr_AverageY= *(Average_Values+1);

                if (average_update_flag == false) {

                    Pre_AverageX = Curr_AverageX;
                    Pre_AverageY = Curr_AverageY;
                    average_update_flag = true;
                }

                //  ctx->periodic_adv_raw_data[12] = (uint8_t)(AverageX);  // Set the appropriate value for the 13th byte
                //  ctx->periodic_adv_raw_data[13] = (uint8_t)(AverageY);  // Set the appropriate value for the 14th byte

                MY_LOGD(LOG_TAG_Central, "Current AverageX: %f, Current AverageY: %f\n", Curr_AverageX, Curr_AverageY);
                MY_LOGD(LOG_TAG_Central, "Previous AverageX: %f, Previous AverageY: %f\n", Pre_AverageX, Pre_AverageY);
                MY_LOGD(LOG_TAG_Central, "counter value in periodic adv data: %d\n", ctx->periodic_adv_raw_data[13]);
            }

#if (CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH)

            adv_msg_t msg = {0};

            /* ---------------- Update PA data ---------------- */

            if (ctx->periodic_adv_raw_data_len == 0 || ctx->periodic_adv_raw_data_len > sizeof(msg.data)) {

                MY_LOGE(LOG_TAG_Central, "Invalid CENTRAL PA data length: %u", (unsigned int)ctx->periodic_adv_raw_data_len);
            }

            else {

                memset(&msg, 0, sizeof(msg));
                msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
                msg.command = ADV_CMD_UPDATE_PA_DATA;

                memcpy(msg.data, ctx->periodic_adv_raw_data, ctx->periodic_adv_raw_data_len);

                msg.data_len = ctx->periodic_adv_raw_data_len;

                /*   MY_LOGD(LOG_TAG_Central,
                                          "PA_UPDATE_QUEUE_SEND: len=%d byte28=%c hex=%02X",
                                          ctx->periodic_adv_raw_data_len,
                                          ctx->periodic_adv_raw_data[28],
                                          ctx->periodic_adv_raw_data[28]);
   */
                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                    MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL UPDATE_PA_DATA");
                }
            }
            // for beacon ext adv data

            if (ctx->periodic_beacon_data_len == 0 || ctx->periodic_beacon_data_len > sizeof(msg.data)) {

                MY_LOGE(LOG_TAG_Central, "Invalid CENTRAL BEACON data length: %u", (unsigned int)ctx->periodic_beacon_data_len);
            }

            else {

                if (ctx->adv_status.ext_adv_beacon_started == true) {

                    // stop
                    memset(&msg, 0, sizeof(msg));
                    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL_BEACON;
                    msg.command = ADV_CMD_STOP_EXT;

                    if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                        MY_LOGE(LOG_TAG_Central, "Failed to queue ADV CENTRAL STOP_EXT for beacon");
                    }
                }

                // update the beacon data with the new periodic adv data
                memset(&msg, 0, sizeof(msg));
                msg.handle_ext = EXT_ADV_HANDLE_CENTRAL_BEACON;
                msg.command = ADV_CMD_UPDATE_EXT_DATA;

                memcpy(msg.data, ctx->periodic_beacon_data, ctx->periodic_beacon_data_len);

                msg.data_len = ctx->periodic_beacon_data_len;

                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
                    MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL UPDATE_EXT_DATA");
                }

                // start
                memset(&msg, 0, sizeof(msg));
                msg.handle_ext = EXT_ADV_HANDLE_CENTRAL_BEACON;
                msg.command = ADV_CMD_START_EXT;

                if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {

                    MY_LOGE(LOG_TAG_Central, "Failed to queue ADV CENTRAL START_EXT for beacon");
                }
            }
            // send data to AWS/MQTT Broker --> MQTT Explorer

            MQTT_Message(0xB0, Curr_AverageX, Curr_AverageY, Pre_AverageX, Pre_AverageY);  // Call the MQTT publish function with the average values

            Pre_AverageX = Curr_AverageX;
            Pre_AverageY = Curr_AverageY;

            // 4. Send START_PA

            //  ctx->adv_message.command = ADV_CMD_START_PA;
            //  xQueueSend(ctx->adv_queue, &ctx->adv_message, 0);

#endif

#if CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH

            // **** OUTSIDE MUTEX — SAFE DELAY ****
            //   vTaskDelay(pdMS_TO_TICKS(10));
            //    MY_LOGE(LOG_TAG_Central, "CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH");
            vTaskDelay(pdMS_TO_TICKS(5000));

#endif
        }
    }
}

// Task to act as a facilitator for sync establish and ACK and sending ACK/SYNC advertisements using notifications from event handler and called periodic_sync_worker_task
static void central_pa_ack_sync_task(void *arg) {

    uint32_t notification_value = 0;

    int position = 28;  // offset to add MAC address bytes
    ble_node_context_t *ctx = (ble_node_context_t *)arg;

    for (;;) {

        // Wait for notification from the event handler
        xTaskNotifyWait(0, ULONG_MAX, &notification_value, portMAX_DELAY);  // ULONG_MAX set bits on exit

        // Stop scanning when notified
        if (notification_value & Periodic_PDU_SYN) {  // if sync notification received

            MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_CYAN "Sync Established ....(scanning task)" COLOR_RESET);

            memcpy(&ctx->ack_data[8], "CEN", 3);
            memcpy(&ctx->ack_data[12], "SYN", 3);
            //    memcpy(&ctx->ack_data[31],"CODE",4);
            //    memcpy(&ctx->ack_data[36],&mac_addr[3],3);           // parent MAC address last 3 bytes copied to ack data

            for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

                position = 28 + i * 3;  // update position to add MAC address bytes

                if (position + 3 > 36)
                    break;  // check if buffer is full

                if (ctx->central_tracked_devices[i].sync_ack_pending) {  // if new data flag is set then update the last 3 bytes of MAC address

                    memcpy(&ctx->ack_data[position], ctx->central_tracked_devices[i].synced_peer_last3_mac, 3);
                    ctx->central_tracked_devices[i].sync_ack_pending = false;
                    // ack_ready = true;
                }
            }
        } else if (notification_value & Periodic_PDU_ACK) {  // if periodic ack notification received

            //  ESP_LOGI(LOG_TAG, "Preparing periodic ACK for all devices...");

            memcpy(&ctx->ack_data[8], "CEN", 3);
            memcpy(&ctx->ack_data[12], "ACK", 3);
            memcpy(&ctx->ack_data[23], "CODE", 4);

            memcpy(&ctx->ack_data[27], get_shared_code(ctx), 1);  // copy the code to ack data
                                                                  //  memcpy(&ctx->ack_data[36],&mac_addr[3],3);           // parent MAC address last 3 bytes copied to ack data

            for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

                position = 28 + i * 3;  // update position to add MAC address bytes

                if (position + 3 > 36)
                    break;  // check if buffer is full

                if (ctx->central_tracked_devices[i]
                        .ext_ack_pending) {  //} && central_tracked_devices[i].last_seen_time != 0  && (current_time - central_tracked_devices[i].last_seen_time)<TIMEOUT_PERIOD_MS){ // if new data
                                             //flag is set then update the last 3 bytes of MAC address

                    memcpy(&ctx->ack_data[position], ctx->central_tracked_devices[i].synced_peer_last3_mac, 3);

                    ctx->central_tracked_devices[i].ext_ack_pending = false;
                    ack_ready = true;
                    //     ctx->central_tracked_devices[i].Periodic_Adv_Sync_Estab = false; // reset the periodic adv sync established flag
                }
            }
        }

        if (ack_ready) {

            ack_ready = false;

            MY_LOGD(LOG_TAG_Central, "ack ready.... ");

            adv_msg_t msg = {0};
            msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;

            // UPDATE EXT ADV DATA
            msg.command = ADV_CMD_UPDATE_EXT_DATA;
            memcpy(msg.data, ctx->ack_data, sizeof(ctx->ack_data));
            msg.data_len = sizeof(ctx->ack_data);

            if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
                MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL UPDATE_EXT_DATA");
            }

            // STOP EXT ADV
            memset(&msg, 0, sizeof(msg));
            msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
            msg.command = ADV_CMD_STOP_EXT;

            if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
                MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL STOP_EXT");
            }

            // START EXT ADV
            memset(&msg, 0, sizeof(msg));
            msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
            msg.command = ADV_CMD_START_EXT;

            if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
                MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL START_EXT");
            }

            ctx->central_auto_stop_ext_adv = false;

            // STOP EXT ADV again after ACK burst
            memset(&msg, 0, sizeof(msg));
            msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
            msg.command = ADV_CMD_STOP_EXT;

            if (xQueueSend(ctx->adv_queue, &msg, 0) != pdTRUE) {
                MY_LOGE(LOG_TAG_Central, "Failed to queue CENTRAL STOP_EXT after ACK burst");
            }

            xTaskNotify(ctx->central_periodic_adv_updateTaskHandle, START_PERIODIC_ADV, eSetBits);
        }
    }
}

void start_central_mode(ble_node_context_t *ctx) {

    ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL] = false;
    ctx->adv_status.ext_adv_beacon_set_params[EXT_ADV_HANDLE_CENTRAL] = false;
    ctx->adv_status.ext_adv_beacon_data_raw[EXT_ADV_HANDLE_CENTRAL] = false;

    ctx->adv_status.periodic_adv_data_raw[EXT_ADV_HANDLE_CENTRAL] = false;
    ctx->adv_status.periodic_adv_set_params[EXT_ADV_HANDLE_CENTRAL] = false;
    ctx->adv_status.periodic_adv_started[EXT_ADV_HANDLE_CENTRAL] = false;

    ctx->adv_status.periodic_adv_data_raw[EXT_ADV_HANDLE_CENTRAL_BEACON] = false;
    ctx->adv_status.periodic_adv_set_params[EXT_ADV_HANDLE_CENTRAL_BEACON] = false;
    ctx->adv_status.periodic_adv_started[EXT_ADV_HANDLE_CENTRAL_BEACON] = false;

    ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL_BEACON] = false;
    ctx->adv_status.ext_adv_data_raw[EXT_ADV_HANDLE_CENTRAL_BEACON] = false;
    ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_CENTRAL_BEACON] = false;

    ctx->adv_status.ext_adv_data_raw[EXT_ADV_HANDLE_CENTRAL] = false;
    ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_CENTRAL] = false;
    ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL] = false;

    // ctx->adv_status.ext_scan_started = false;   // i commented this line because it stops the "scanning stops" in "adv_command_executor_task"
    ctx->adv_status.ext_scan_params_set = false;
    ctx->central_auto_stop_ext_adv = false;

    ctx->adv_wait.pending_cmd = ADV_CMD_NONE;
    ctx->adv_message.command = ADV_CMD_NONE;
    memset(ctx->adv_message.data, 0, sizeof(ctx->adv_message.data));
    ctx->adv_message.data_len = 0;
    ctx->adv_message.term_sync = 0;
    ctx->SID = 0;
    /* ctx->shared_code = 0; */

    if (ctx->my_new_role != ROLE_REPEATER && ctx->my_current_role != ROLE_REPEATER) {

        ctx->shared_code = 0;
    }

    else {

        update_shared_code_and_adv_payload(ctx, "start_central_mode / REPEATER keep code");
    }

    // ctx->adv_context.queue = NULL;

    for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

        memset(ctx->central_tracked_devices[i].addr, 0, 6);
        memset(ctx->central_tracked_devices[i].discovery_last3_mac, 0, 3);
        memset(ctx->central_tracked_devices[i].synced_peer_last3_mac, 0, 3);
        ctx->central_tracked_devices[i].addr_type = 0xFF;
        ctx->central_tracked_devices[i].sid = 0xFF;
        //    ctx->central_tracked_devices[i].sync_handle = 0xFFFF;
        ctx->central_tracked_devices[i].is_synced = false;
        ctx->central_tracked_devices[i].is_syncing = false;
        ctx->central_tracked_devices[i].in_use = false;
        ctx->central_tracked_devices[i].is_queued = false;
        ctx->central_tracked_devices[i].ext_ack_pending = false;
        ctx->central_tracked_devices[i].sync_stage = STAGE_IDLE;
        ctx->central_tracked_devices[i].Periodic_Adv_Sync_Estab = false;
    }

    memset(ctx->central_tracked_devices, 0, sizeof(ctx->central_tracked_devices));

    for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

        ctx->central_tracked_devices[i].sync_handle = 0xFFFF;
    }

    Central_init_Ext_adv_timer(ctx);  // Initialize the advertising timer for central role
    Central_init_PA_timer(ctx);       // Initialize the advertising timer for central role

    if (!esp_timer_is_active(ctx->central_three_seconds_timer_scan)) {

        ESP_ERROR_CHECK(esp_timer_start_once(ctx->central_three_seconds_timer_scan, 5000000));
    }

    if (Wifi_Read_Via_Buttons == true) {

        // read the wifi or sync code via button press and update the ext adv data at boot time , if the flag is set to true which is set by pressing the button during init

        update_shared_code_and_adv_payload(ctx, "Central Boot time ");  // update the shared code and adv payload
    }

    //  init_periodic_devices();   // doubt .................************************

    ctx->central_syncQueue = xQueueCreate(MAX_SYNC_QUEUE, sizeof(periodic_device_info_t *));

    if (ctx->central_syncQueue == NULL) {
        MY_LOGE(LOG_TAG_Common, "gap register error, error code");
        return;
    }

    adv_msg_t msg = {0};
    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
    // SET SCAN
    msg.command = ADV_CMD_SET_SCAN;
    msg.data_len = 0;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // START SCAN
    msg.command = ADV_CMD_START_SCAN;
    msg.data_len = 0;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // SET EXT ADV PARAMS
    msg.command = ADV_CMD_SET_EXT_PARAMS;
    msg.data_len = 0;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // UPDATE EXT ADV DATA
    msg.command = ADV_CMD_UPDATE_EXT_DATA;
    memcpy(msg.data, ctx->raw_ext_adv_data_2m, sizeof(ctx->raw_ext_adv_data_2m));
    msg.data_len = sizeof(ctx->raw_ext_adv_data_2m);

    xQueueSend(ctx->adv_queue, &msg, 0);

    xTaskCreate(central_pa_ack_sync_task, "central_pa_ack_sync_task", 4096, ctx, 5,
                &ctx->central_pa_ack_syncTaskHandle);  // Notify the central_pa_ack_sync_task(ack ,sync ,adv_data[] to send the prph/repeater )
    xTaskCreate(periodic_adv_update_task, "periodic_adv_update_task", 4096, ctx, 7,
                &ctx->central_periodic_adv_updateTaskHandle);  // Notify the central_pa_ack_sync_task(ack ,sync ,adv_data[] to send the prph/repeater )
    xTaskCreate(periodic_sync_worker_task, "periodic_sync_worker_task", 4096, ctx, 6, &ctx->central_sync_worker_TaskHandle);
}

void stop_central_mode(ble_node_context_t *ctx) {

    adv_msg_t msg = {0};
    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;

    // 1) STOP SCAN

    msg.command = ADV_CMD_STOP_SCAN;
    msg.data_len = 0;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // 2) TERMINATE PA

    // for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {
    for (int i = 0; i < 1; i++) {

        //  if (ctx->central_tracked_devices[i].sync_handle != 0xFFFF) {

        // FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_sync_terminate(ctx->central_tracked_devices[i].sync_handle),ctx->test_sem);

        //  ctx->adv_message.term_sync = ctx->central_tracked_devices[i].sync_handle;
        //  ctx->adv_message.command = ADV_CMD_TERMINATE_SYNC;

        uint16_t sh = ctx->central_tracked_devices[i].sync_handle;

        if (sh != 0xFFFF) {

            memset(&msg, 0, sizeof(msg));
            msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
            msg.term_sync = sh;
            msg.command = ADV_CMD_TERMINATE_SYNC;

            xQueueSend(ctx->adv_queue, &msg, 0);

            ctx->central_tracked_devices[i].sync_handle = 0xFFFF;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // STOP PA
    memset(&msg, 0, sizeof(msg));
    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
    msg.command = ADV_CMD_STOP_PA;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // STOP EXT ADV
    memset(&msg, 0, sizeof(msg));
    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
    msg.command = ADV_CMD_STOP_EXT;
    xQueueSend(ctx->adv_queue, &msg, 0);

    // REMOVE EXT ADV PARAMS
    memset(&msg, 0, sizeof(msg));
    msg.handle_ext = EXT_ADV_HANDLE_CENTRAL;
    msg.command = ADV_CMD_REMOVE_EXT_PARAMS;
    xQueueSend(ctx->adv_queue, &msg, 0);
    // 7) CLEAR DEVICE TABLE

    for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {

        memset(ctx->central_tracked_devices[i].addr, 0, 6);
        memset(ctx->central_tracked_devices[i].discovery_last3_mac, 0, 3);
        memset(ctx->central_tracked_devices[i].synced_peer_last3_mac, 0, 3);
        ctx->central_tracked_devices[i].addr_type = 0xFF;
        ctx->central_tracked_devices[i].sid = 0xFF;
        //    ctx->central_tracked_devices[i].sync_handle = 0xFFFF;
        ctx->central_tracked_devices[i].is_synced = false;
        ctx->central_tracked_devices[i].is_syncing = false;
        ctx->central_tracked_devices[i].in_use = false;
        ctx->central_tracked_devices[i].is_queued = false;
        ctx->central_tracked_devices[i].ext_ack_pending = false;

        ctx->central_tracked_devices[i].sync_stage = STAGE_IDLE;
        ctx->central_tracked_devices[i].Periodic_Adv_Sync_Estab = false;

        // esp_ble_gap_periodic_adv_sync_terminate(ctx->central_tracked_devices[i].sync_handle );
    }

    ctx->shared_code = 0;

    if (ctx->central_adv_timer != NULL) {

        esp_err_t err = esp_timer_stop(ctx->central_adv_timer);

        esp_timer_delete(ctx->central_adv_timer);
        ctx->central_adv_timer = NULL;

        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            MY_LOGE("Timer", "Failed to stop timer: %s", esp_err_to_name(err));
        }
    }

    if (ctx->central_PA_timer != NULL) {

        esp_err_t err = esp_timer_stop(ctx->central_PA_timer);

        esp_timer_delete(ctx->central_PA_timer);
        ctx->central_PA_timer = NULL;

        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            MY_LOGE("Timer", "Failed to stop timer: %s", esp_err_to_name(err));
        }
    }

    ble_node_ctx.central_auto_stop_ext_adv = false;

    // 6) NOW delete tasks / queues safely

    if (ctx->central_pa_ack_syncTaskHandle) {

        vTaskDelete(ctx->central_pa_ack_syncTaskHandle);
        ctx->central_pa_ack_syncTaskHandle = NULL;
    }

    if (ctx->central_periodic_adv_updateTaskHandle) {

        vTaskDelete(ctx->central_periodic_adv_updateTaskHandle);
        ctx->central_periodic_adv_updateTaskHandle = NULL;
    }

    if (ctx->central_sync_worker_TaskHandle) {

        vTaskDelete(ctx->central_sync_worker_TaskHandle);
        ctx->central_sync_worker_TaskHandle = NULL;
    }

    if (ctx->central_syncQueue) {

        vQueueDelete(ctx->central_syncQueue);
        ctx->central_syncQueue = NULL;
    }

    if (ctx->adv_queue) {

        xQueueReset(ctx->adv_queue);
    }

    memset(ctx->central_tracked_devices, 0, sizeof(ctx->central_tracked_devices));

    for (int i = 0; i < CENTRAL_MAX_PERIODIC_DEVICES; i++) {
        ctx->central_tracked_devices[i].sync_handle = 0xFFFF;
    }
}