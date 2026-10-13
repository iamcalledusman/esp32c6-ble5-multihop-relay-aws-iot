#include "ble_node_context.h"
#include "ble_sync_utils.h"
#include "central_mode.h"
#include "common_mode.h"
#include "device_type_utils.h"
#include "esp_err.h"
#include "log.h"
#include "peripheral_mode.h"
#include "repeater_mode.h"

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

transition_action_t transition_table[7] = {

    [TRANS_PRPH_CEN] = {.start_prph = false, .start_cen = true, .gap_event_handler_stop_role = ROLE_PERIPHERAL, .stop_prph = true, .stop_cen = false},
    [TRANS_CEN_PRPH] = {.start_prph = true, .start_cen = false, .gap_event_handler_stop_role = ROLE_CENTRAL, .stop_prph = false, .stop_cen = true},
    [TRANS_PRPH_REPEATER] = {.start_prph = false, .start_cen = true, .gap_event_handler_stop_role = ROLE_PERIPHERAL, .stop_prph = false, .stop_cen = false},
    //[TRANS_REPEATER_PRPH]={.start_prph=false, .start_cen=false, .gap_event_handler_stop_role=ROLE_CENTRAL, .stop_prph=false, .stop_cen=true},  this logic is simple just have to stop the central but
    // issue is that , central unable to get the PA
    [TRANS_REPEATER_PRPH] =
        {.start_prph = true,
         .start_cen = false,
         .gap_event_handler_stop_role = ROLE_REPEATER,
         .stop_prph = true,
         .stop_cen = true},  // this logic is not good as compared to above , as have to stop both and then start the prph again , it gave me errors while commands runs but roles are working fine!
    [TRANS_NONE_PRPH] = {.start_prph = true, .start_cen = false, .gap_event_handler_stop_role = ROLE_PERIPHERAL, .stop_prph = false, .stop_cen = false},
    [TRANS_NONE_CEN] = {.start_prph = false, .start_cen = true, .gap_event_handler_stop_role = ROLE_CENTRAL, .stop_prph = false, .stop_cen = false},
    [TRANS_NONE] = {.start_prph = false, .start_cen = false, .gap_event_handler_stop_role = ROLE_UNDEFINED, .stop_prph = false, .stop_cen = false}

};

TaskHandle_t deferredCentralStartTaskHandle = NULL;

static esp_ble_gap_ext_adv_t ext_adv_Central[2] = {
    // extended adv params
    // instance, duration, period
    [0] = {EXT_ADV_HANDLE_CENTRAL, 0, 0},
    [1] = {EXT_ADV_HANDLE_CENTRAL_BEACON, 0, 0},

};

static esp_ble_gap_ext_adv_t ext_adv_Peripheral[2] = {
    // extended adv params
    // instance, duration, period
    [0] = {EXT_ADV_HANDLE_PERIPHERAL, 0, 0},
    [1] = {EXT_ADV_HANDLE_PERIPHERAL_BEACON, 0, 0},

};

char *get_power_string(power_type_t power) {

    switch (power) {

    case usb:

        return "USB";
        break;

    case battery:

        return "Battery";
        break;

    default:
        break;
    }

    return "Unknown";
}

char *get_sync_string(sync_type_t sync) {

    switch (sync) {

    case synced:

        return "Synced";
        break;

    case Not_synced:

        return "Not Synced";
        break;

    default:
        break;
    }

    return "Unknown";
}

char *get_wifi_string(wifi_type_t wifi) {

    switch (wifi) {

    case No_wifi:

        return "No Wifi";
        break;

    case wifi_5_6:

        return "wifi 5 & 6";
        break;

    case wifi_6:

        return "wifi 6";
        break;

    case wifi_5:

        return "wifi 5";
        break;

    default:
        break;
    }

    return "Unknown";
}

static inline void set_pending_adv_cmd(ble_node_context_t *ctx, adv_command_t cmd) {

    ctx->adv_wait.pending_cmd = cmd;
    ctx->adv_wait.cmd_result = false;  // Reset the command result to false
}

void init_gpio_inputs(void) {

    gpio_config_t io_conf = {

        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 0,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // or GPIO_PULLDOWN_ENABLE if needed
        .pull_down_en = GPIO_PULLDOWN_DISABLE

    };

    for (int i = 0; i < sizeof(input_pins) / sizeof(input_pins[0]); i++) {

        io_conf.pin_bit_mask = 1ULL << input_pins[i];
        gpio_config(&io_conf);
    }

    MY_LOGD("GPIO", "Input GPIOs initialized.");
}

void deferredCentralStartTask(void *pvParam) {
    ble_node_context_t *ctx = (ble_node_context_t *)pvParam;

    while (1) {
        // Wait until notified (from switch_role_if_needed or elsewhere)

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 💡 Now the GAP handler is already fully registered and active
        start_central_mode(ctx);

        MY_LOGD("DEFERRED", "✅ Central mode started");
    }
}

void read_device_inputs(device_type_t *device) {

    device->power = (gpio_get_level(input_pins[0]) == 1) ? battery : usb;       // usb=0 ,battery=1
    device->sync = (gpio_get_level(input_pins[1]) == 1) ? Not_synced : synced;  // Not_synced=1 , synced=0

    int wifi_value = (gpio_get_level(input_pins[3]) << 1) | (gpio_get_level(input_pins[2]));  // bit1|bit0  , GPIO_INPUT_6 & GPIO_INPUT_7

    switch (wifi_value) {
    case 0:
        device->wifi = No_wifi;
        break;

    case 1:
        device->wifi = wifi_5_6;
        break;

    case 2:
        device->wifi = wifi_6;
        break;

    case 3:
        device->wifi = wifi_5;
        break;

    default:
        break;
    }

    MY_LOGD(LOG_TAG_Common, "Power : %s  Sync : %s  Wifi :  %s ", get_power_string(device->power), get_sync_string(device->sync), get_wifi_string(device->wifi));
}

/* void Read_Wifi_Status_at_boot(ble_node_context_t *ctx){



        for (int i = 0; i < sizeof(input_pins) / sizeof(input_pins[0]); i++) {
            int level = gpio_get_level(input_pins[i]);
            switch (i)
            {
            case 0:
                ctx->device.power = (level==1)?battery:usb;              // usb=0 ,battery=1
                break;
            case 1:
                ctx->device.sync= (level==1)?Not_synced:synced;         // Not_synced=1 , synced=0
                break;

            case 2:
            case 3:
                int wifi_bit0 = gpio_get_level(input_pins[2]); // GPIO_INPUT_6
                int wifi_bit1 = gpio_get_level(input_pins[3]); // GPIO_INPUT_7
                int wifi_value = (wifi_bit1 << 1) | wifi_bit0;

                switch (wifi_value)
                {
                case 0:
                    ctx->device.wifi = No_wifi;
                    break;

                 case 1:
                    ctx->device.wifi = wifi_5_6;
                    break;

                 case 2:
                    ctx->device.wifi = wifi_6;
                    break;

                 case 3:
                    ctx->device.wifi = wifi_5;
                    break;


                default:
                    break;
                }

                break;

            default:
                break;
            }

        }

        MY_LOGW(LOG_TAG_Common, "Power : %s  Sync : %s  Wifi :  %s ", get_power_string(ctx->device.power),get_sync_string(ctx->device.sync),get_wifi_string(ctx->device.wifi));

        uint8_t device_code;

        char * device_type = get_type(&ctx->device, &device_code);  // Get the device type(A0,C0...) and code(1-9) ,from look_up_table using malloc , so device_type need to be freed after use

         MY_LOGD("Type and code is ", "%s / %d", device_type,device_code);

        update_role_and_type(ctx,EVENT_BOOT,device_code);// update the role and type on boot event, on the basis of device info in ble_node_ctx.device


        free(device_type); // Free the memory allocated by get_type

}  */
// Extended scanning parameters (Central Role)
static esp_ble_ext_scan_params_t ext_scan_params = {

    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    .cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_CODE_MASK,  // ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK |
    .coded_cfg = {BLE_SCAN_TYPE_PASSIVE, 50, 50},

};

/* static void ack_timeout_cb(void *arg)
{
    ble_node_context_t *ctx = (ble_node_context_t *)arg;
   // esp_timer_stop(ctx->ext_adv_ack_timer); // Stop the timer to prevent further callbacks

}

void init_ack_timer(ble_node_context_t *ctx)
{
    const esp_timer_create_args_t args = {

        .callback = ack_timeout_cb,
        .arg = ctx,
        .name = "ack_timeout"

    };

    ESP_ERROR_CHECK(esp_timer_create(&args, &ctx->ext_adv_ack_timer));
}
 */
bool Cen_Ack_Mac_Extract(ble_node_context_t *ctx, uint8_t *adv_data, uint8_t len, char *type) {  // extract the MAC address from the adv data from central device

    for (int i = 22; i < len - 3; i += 3) {  // loop through the adv data to find the MAC address

        if (memcmp(&adv_data[i], &ctx->mac_addr[3], 3) == 0) {

            if (type && strncmp(type, "SYN", 3) == 0) {
                MY_LOGI(LOG_TAG_Common, "SYN MAC (Cen_Ack_Mac_Extract): %02X:%02X:%02X", adv_data[i], adv_data[i + 1], adv_data[i + 2]);
            }

            else if (type && strncmp(type, "ACK", 3) == 0) {
                MY_LOGI(LOG_TAG_Common, "ACK MAC (Cen_Ack_Mac_Extract): %02X:%02X:%02X", adv_data[i], adv_data[i + 1], adv_data[i + 2]);
            }

            return 1;
        }
    }

    return 0;
}

void ble_get_mac_address(uint8_t *mac) {  // read mac address from the chip

    esp_err_t ret = esp_read_mac(mac, ESP_MAC_BT);  // read mac address from system api

    if (ret == ESP_OK) {

        MY_LOGD(LOG_TAG_Common, "MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    } else {

        MY_LOGE(LOG_TAG_Common, "Failed to get MAC address, error: %s", esp_err_to_name(ret));
    }
}
// common role end

void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {

    ble_node_context_t *ctx = &ble_node_ctx;

    int role_for_gap = ctx->my_current_role;

    if (ctx->adv_wait.pending_cmd != ADV_CMD_NONE) {

        role_for_gap = ctx->last_known_role;

        MY_LOGD(LOG_TAG_Common, "Gap event handler Last Known ROLE  %d ", role_for_gap);
    }

    MY_LOGI(LOG_TAG_Common, "Gap event handler Current ROLE  %d ", role_for_gap);

    switch (role_for_gap) {

    case ROLE_CENTRAL:
        gap_event_handler_Central(event, param, ctx);

        break;
    case ROLE_PERIPHERAL:
        gap_event_handler_Peripheral(event, param, ctx);

        break;
    case ROLE_REPEATER:
        gap_event_handler_Central(event, param, ctx);
        gap_event_handler_Peripheral(event, param, ctx);

        break;
    default:
        MY_LOGW(LOG_TAG_Common, "Invalid role: %d", role_for_gap);
        for (int i = 0; i < 10; ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
            MY_LOGW(LOG_TAG_Common, "Still stuck! Waiting for role update...");
        }
        break;
    }
}

static const char *adv_command_to_string(adv_command_t command) {

    switch (command) {

    case ADV_CMD_NONE:
        return "ADV_CMD_NONE";
    case ADV_CMD_START_EXT:
        return "ADV_CMD_START_EXT";
    case ADV_CMD_STOP_EXT:
        return "ADV_CMD_STOP_EXT";
    case ADV_CMD_UPDATE_EXT_DATA:
        return "ADV_CMD_UPDATE_EXT_DATA";
    case ADV_CMD_SET_EXT_PARAMS:
        return "ADV_CMD_SET_EXT_PARAMS";
    case ADV_CMD_REMOVE_EXT_PARAMS:
        return "ADV_CMD_REMOVE_EXT_PARAMS";
    case ADV_CMD_START_SCAN:
        return "ADV_CMD_START_SCAN";
    case ADV_CMD_STOP_SCAN:
        return "ADV_CMD_STOP_SCAN";
    case ADV_CMD_SET_SCAN:
        return "ADV_CMD_SET_SCAN";
    case ADV_CMD_SET_PA_PARAMS:
        return "ADV_CMD_SET_PA_PARAMS";
    case ADV_CMD_START_PA:
        return "ADV_CMD_START_PA";
    case ADV_CMD_STOP_PA:
        return "ADV_CMD_STOP_PA";
    case ADV_CMD_UPDATE_PA_DATA:
        return "ADV_CMD_UPDATE_PA_DATA";
    case ADV_CMD_TERMINATE_SYNC:
        return "ADV_CMD_TERMINATE_SYNC";

    default:
        return "Unknown Command";
    }
}

static const char *adv_handle_to_string(uint8_t handle) {

    switch (handle) {

    case EXT_ADV_HANDLE_CENTRAL:
        return "EXT_ADV_HANDLE_CENTRAL";
    case EXT_ADV_HANDLE_PERIPHERAL:
        return "EXT_ADV_HANDLE_PERIPHERAL";
    case EXT_ADV_HANDLE_CENTRAL_BEACON:
        return "EXT_ADV_HANDLE_CENTRAL_BEACON";
    case EXT_ADV_HANDLE_PERIPHERAL_BEACON:
        return "EXT_ADV_HANDLE_PERIPHERAL_BEACON";
    default:
        return "Unknown Handle";
    }
}

inline static void log_adv_command_handle(adv_command_t command, uint8_t handle) {

    MY_LOGD(LOG_TAG_Common, "Handle : %s , Command :%s ", adv_handle_to_string(handle), adv_command_to_string(command));
}

void adv_command_executor_task(void *param) {

    ble_node_context_t *ctx = (ble_node_context_t *)param;

    while (1) {

        if (xQueueReceive(ctx->adv_queue, &ctx->adv_message, portMAX_DELAY) == pdTRUE) {

            // MY_LOGV(LOG_TAG_Common, "Command: %d  , Handle %d ", ctx->adv_message.command, ctx->adv_message.handle_ext);

            log_adv_command_handle(ctx->adv_message.command, ctx->adv_message.handle_ext);

            switch (ctx->adv_message.command) {

            case ADV_CMD_START_EXT:  // ok

                if (!ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                    set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_start(NUM_EXT_ADV, &ext_adv_Peripheral[0]), ctx->test_sem);

                    ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL] = true;  // set the flag to true , means ext adv started
                    MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_BLUE "[FLOW-11] PERIPHERAL_NORMAL_EXT_ADV_STARTED (EXECUTOR_TASK)" COLOR_RESET);
                    vTaskDelay(pdMS_TO_TICKS(4000));  // i have put the 4 secs delay here , in future timer is required here because non blocking and more professional way to handle this

                }

                else if (!ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                    set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_start(NUM_EXT_ADV, &ext_adv_Central[0]), ctx->test_sem);

                    ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL] = true;  // set the flag to true , means ext adv started
                    MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_BLUE "[FLOW-11] CENTRAL_NORMAL_EXT_ADV_STARTED (EXECUTOR_TASK)" COLOR_RESET);
                    vTaskDelay(pdMS_TO_TICKS(4000));  // i have put the 4 secs delay here , in future timer is required here because non blocking and more professional way to handle this
                }

                if (!ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL_BEACON) {

                    set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_start(NUM_EXT_ADV, &ext_adv_Peripheral[1]), ctx->test_sem);
                    ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = true;  // set the flag to true , means ext adv beacon started
                    MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_BLUE "[FLOW-11A] PERIPHERAL_BEACON_EXT_ADV_STARTED (EXECUTOR_TASK)" COLOR_RESET);
                    vTaskDelay(pdMS_TO_TICKS(20));

                }

                else if (!ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL_BEACON] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL_BEACON) {

                    set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_start(NUM_EXT_ADV, &ext_adv_Central[1]), ctx->test_sem);

                    ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL_BEACON] = true;  // set the flag to true , means ext adv beacon started
                    MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_BLUE "[FLOW-11A] CENTRAL_BEACON_EXT_ADV_STARTED (EXECUTOR_TASK)" COLOR_RESET);
                    vTaskDelay(pdMS_TO_TICKS(20));
                }

                break;

            case ADV_CMD_STOP_EXT:  // ok

                if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {
                    if (ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL]) {
                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                        FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_stop(NUM_EXT_ADV, &ext_adv_Peripheral[0].instance), ctx->test_sem);

                        ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL] = false;
                        MY_LOGD(LOG_TAG_Common, "Ext Adv Stopped Peripheral...");
                    }
                }

                else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {
                    if (ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL]) {

                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                        FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_stop(NUM_EXT_ADV, &ext_adv_Central[0].instance), ctx->test_sem);

                        ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL] = false;
                        MY_LOGD(LOG_TAG_Common, "Ext Adv Stopped Central...");
                    }
                }

                else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL_BEACON) {
                    if (ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON]) {
                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                        FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_stop(NUM_EXT_ADV, &ext_adv_Peripheral[1].instance), ctx->test_sem);

                        ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = false;

                        MY_LOGD(LOG_TAG_Common, "Ext Adv Beacon Stopped Peripheral...");
                    }
                }

                else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL_BEACON) {
                    if (ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL_BEACON]) {
                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                        FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_stop(NUM_EXT_ADV, &ext_adv_Central[1].instance), ctx->test_sem);

                        ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL_BEACON] = false;

                        MY_LOGD(LOG_TAG_Common, "Ext Adv Beacon Stopped Central...");
                    }
                }

                break;

            case ADV_CMD_UPDATE_EXT_DATA:

                if (ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_PERIPHERAL] && !ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL] &&
                    ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_config_ext_adv_data_raw(EXT_ADV_HANDLE_PERIPHERAL, ctx->adv_message.data_len, ctx->adv_message.data), ctx->test_sem);

                    ctx->adv_status.ext_adv_data_raw[EXT_ADV_HANDLE_PERIPHERAL] = true;

                }

                // else if (ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_CENTRAL] && !ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL] && ctx->adv_message.handle_ext ==
                // EXT_ADV_HANDLE_CENTRAL) {

                else if (ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_CENTRAL] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_config_ext_adv_data_raw(EXT_ADV_HANDLE_CENTRAL, ctx->adv_message.data_len, ctx->adv_message.data), ctx->test_sem);

                    ctx->adv_status.ext_adv_data_raw[EXT_ADV_HANDLE_CENTRAL] = true;
                    MY_LOGD(LOG_TAG_Common, "Ext Adv UPDATE RAW DATA Central...");
                }

                else if (ctx->adv_status.ext_adv_beacon_set_params[EXT_ADV_HANDLE_PERIPHERAL_BEACON] && !ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] &&
                         ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL_BEACON) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_config_ext_adv_data_raw(EXT_ADV_HANDLE_PERIPHERAL_BEACON, ctx->adv_message.data_len, ctx->adv_message.data), ctx->test_sem);

                    ctx->adv_status.ext_adv_beacon_data_raw[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = true;
                    MY_LOGD(LOG_TAG_Common, "Ext Adv Beacon UPDATE RAW DATA Peripheral...");
                }

                else if (ctx->adv_status.ext_adv_beacon_set_params[EXT_ADV_HANDLE_CENTRAL_BEACON] && !ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL_BEACON] &&
                         ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL_BEACON) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_config_ext_adv_data_raw(EXT_ADV_HANDLE_CENTRAL_BEACON, ctx->adv_message.data_len, ctx->adv_message.data), ctx->test_sem);

                    ctx->adv_status.ext_adv_beacon_data_raw[EXT_ADV_HANDLE_CENTRAL_BEACON] = true;
                    MY_LOGD(LOG_TAG_Common, "Ext Adv Beacon UPDATE RAW DATA Central...");
                }

                break;

            case ADV_CMD_SET_EXT_PARAMS:

                if (!ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_PERIPHERAL] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_params(EXT_ADV_HANDLE_PERIPHERAL, &ext_adv_params_2M_Peripheral), ctx->test_sem);

                    ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_PERIPHERAL] = true;
                    MY_LOGD(LOG_TAG_Common, "Ext Adv SET PARAM Peripheral...");
                }

                else if (!ctx->adv_status.ext_adv_started[EXT_ADV_HANDLE_CENTRAL] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_params(EXT_ADV_HANDLE_CENTRAL, &ext_adv_params_2M_Central), ctx->test_sem);

                    ctx->adv_status.ext_adv_set_params[EXT_ADV_HANDLE_CENTRAL] = true;
                    MY_LOGD(LOG_TAG_Common, "Ext Adv SET PARAM Central...");
                }

                else if (!ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_PERIPHERAL_BEACON] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL_BEACON) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_params(EXT_ADV_HANDLE_PERIPHERAL_BEACON, &ext_adv_params_beacon_Peripheral), ctx->test_sem);

                    ctx->adv_status.ext_adv_beacon_set_params[EXT_ADV_HANDLE_PERIPHERAL_BEACON] = true;
                    MY_LOGD(LOG_TAG_Common, "Ext Adv Beacon SET PARAM Peripheral...");
                }

                else if (!ctx->adv_status.ext_adv_beacon_started[EXT_ADV_HANDLE_CENTRAL_BEACON] && ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL_BEACON) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_params(EXT_ADV_HANDLE_CENTRAL_BEACON, &ext_adv_params_beacon_Central), ctx->test_sem);

                    ctx->adv_status.ext_adv_beacon_set_params[EXT_ADV_HANDLE_CENTRAL_BEACON] = true;
                    MY_LOGD(LOG_TAG_Common, "Ext Adv Beacon SET PARAM Central...");
                }

                break;

            case ADV_CMD_REMOVE_EXT_PARAMS:  // ok

                if (!ctx->adv_status.ext_adv_started[ctx->adv_message.handle_ext] &&
                    ctx->adv_status.ext_adv_set_params[ctx->adv_message.handle_ext]) {  // Do NOT allow remove unless EXT ADV is already STOPPED

                    if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_remove(EXT_ADV_HANDLE_PERIPHERAL), ctx->test_sem);

                    } else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_remove(EXT_ADV_HANDLE_CENTRAL), ctx->test_sem);
                    }

                    ctx->adv_status.ext_adv_set_params[ctx->adv_message.handle_ext] = false;  // set the flag to true , means ext adv params set

                    //   MY_LOGI(LOG_TAG_Common,"REMOVE Ext Adv Param...");
                }
                break;

            case ADV_CMD_SET_SCAN:

                if (!ctx->adv_status.ext_scan_started && !ctx->adv_status.ext_scan_params_set && ctx->adv_status.node_scan_active == false) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_set_ext_scan_params(&ext_scan_params), ctx->test_sem);
                    ctx->adv_status.ext_scan_params_set = true;
                    //    MY_LOGV(LOG_TAG_Common,"SET Scan Param...");
                }
                break;

            case ADV_CMD_START_SCAN:

                if (!ctx->adv_status.ext_scan_started && ctx->adv_status.ext_scan_params_set == true && ctx->adv_status.node_scan_active == false) {

                    FUNC_SEND_WAIT_SEM(esp_ble_gap_start_ext_scan(EXT_SCAN_DURATION, EXT_SCAN_PERIOD), ctx->test_sem);
                    ctx->adv_status.ext_scan_started = true;  // set the flag to true , means ext scan started
                    MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_WHITE "Start SCAN..." COLOR_RESET);
                    ctx->adv_status.node_scan_active = true;  // this flag set for repeater role , when prph role starts scanning active , for repeater role we ahve to enable central role so scanning
                                                              // starts again, which creates error

                    //  vTaskDelay(pdMS_TO_TICKS(20));
                }

                break;

            case ADV_CMD_STOP_SCAN:  // ok

                if (ctx->adv_status.ext_scan_started) {

                    set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                    FUNC_SEND_WAIT_SEM(esp_ble_gap_stop_ext_scan(), ctx->test_sem);
                    ctx->adv_status.ext_scan_started = false;  // set the flag to false , means ext scan stopped

                    ctx->adv_status.node_scan_active = false;  // please check this condition, if there is an error while testing utc code update

                    MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_WHITE "Stop SCAN..." COLOR_RESET);
                    //   vTaskDelay(pdMS_TO_TICKS(20));
                }
                break;

            case ADV_CMD_SET_PA_PARAMS:

                //  if(!ctx->adv_status.periodic_adv_started){
                if (!ctx->adv_status.periodic_adv_set_params[ctx->adv_message.handle_ext]) {  ///  suggested by chat gpt .....

                    if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_set_params(EXT_ADV_HANDLE_PERIPHERAL, &periodic_adv_params_peripheral), ctx->test_sem);

                    }

                    else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_set_params(EXT_ADV_HANDLE_CENTRAL, &periodic_adv_params_central), ctx->test_sem);
                    }
                    // FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_set_params(ctx->adv_context.handle_pa , &periodic_adv_params), ctx->test_sem);
                    ctx->adv_status.periodic_adv_set_params[ctx->adv_message.handle_ext] = true;  // set the flag to true , means periodic adv params set
                }
                break;

            case ADV_CMD_START_PA:  // ok

                if (!ctx->adv_status.periodic_adv_started[ctx->adv_message.handle_ext]
                    //   &&ctx->adv_status.ext_adv_started
                    && ctx->adv_status.periodic_adv_data_raw[ctx->adv_message.handle_ext] && ctx->adv_status.periodic_adv_set_params[ctx->adv_message.handle_ext]) {

                    if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                        FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_start(EXT_ADV_HANDLE_PERIPHERAL, true), ctx->test_sem);

                    }

                    else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_start(EXT_ADV_HANDLE_CENTRAL, true), ctx->test_sem);
                    }
                    //  FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_start(ctx->adv_context.handle_pa, true), ctx->test_sem);
                    ctx->adv_status.periodic_adv_started[ctx->adv_message.handle_ext] = true;  // set the flag to true , means periodic adv started
                                                                                               //  MY_LOGI(LOG_TAG_Common,"Start PA Param...");
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                break;

            case ADV_CMD_STOP_PA:  // ok

                if (ctx->adv_status.periodic_adv_started[ctx->adv_message.handle_ext]) {

                    if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv
                        FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_stop(EXT_ADV_HANDLE_PERIPHERAL), ctx->test_sem);
                    }

                    else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                        set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_stop(EXT_ADV_HANDLE_CENTRAL), ctx->test_sem);
                    }
                    //   FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_stop(ctx->adv_context.handle_pa), ctx->test_sem);
                    ctx->adv_status.periodic_adv_started[ctx->adv_message.handle_ext] = false;  // set the flag to false , means periodic adv stopped
                                                                                                //    MY_LOGI(LOG_TAG_Common,"Stop PA Param...");
                                                                                                //  vTaskDelay(pdMS_TO_TICKS(20));
                }
                break;

            case ADV_CMD_UPDATE_PA_DATA:

                if (ctx->adv_status.periodic_adv_set_params[ctx->adv_message.handle_ext]) {  // ctx->adv_status.ext_adv_started &&

                    if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_PERIPHERAL) {

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_config_periodic_adv_data_raw(EXT_ADV_HANDLE_PERIPHERAL, ctx->adv_message.data_len, &ctx->adv_message.data[0], false), ctx->test_sem);

                    }

                    else if (ctx->adv_message.handle_ext == EXT_ADV_HANDLE_CENTRAL) {

                        FUNC_SEND_WAIT_SEM(esp_ble_gap_config_periodic_adv_data_raw(EXT_ADV_HANDLE_CENTRAL, ctx->adv_message.data_len, &ctx->adv_message.data[0], false), ctx->test_sem);
                    }
                    // FUNC_SEND_WAIT_SEM(esp_ble_gap_config_periodic_adv_data_raw(ctx->adv_context.handle_pa, ctx->adv_message.data_len, &ctx->adv_message.data[0], false), ctx->test_sem);

                    // MY_LOGI(LOG_TAG_Common,"Update PA Data...");
                    ctx->adv_status.periodic_adv_data_raw[ctx->adv_message.handle_ext] = true;  // set the flag to true , means periodic adv data raw set
                }
                break;

            case ADV_CMD_TERMINATE_SYNC:  // ok

                MY_LOGW(LOG_TAG_Central, "its central termination sync ........................");

                set_pending_adv_cmd(ctx, ctx->adv_message.command);  // set the pending command to start ext adv

                FUNC_SEND_WAIT_SEM(esp_ble_gap_periodic_adv_sync_terminate(ctx->adv_message.term_sync), ctx->test_sem);

                MY_LOGD(LOG_TAG_Common, "TERMINATE Sync...");

                break;

            default:
                MY_LOGE(LOG_TAG_Central, "Unknown ADV command received in adv_command_executor_task");
                break;
            }
        }
    }
}

void init_bluetooth_stack(ble_node_context_t *ctx) {

    // common_role
    esp_err_t ret;

    //  ble_get_mac_address(ctx->mac_addr);                                                         // Get the BLE MAC address

    // common_role start
    ret = nvs_flash_init();  // Initialize NVS flash memory

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));  // Release the classic BT memory

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();  // Initialize the BT controller config

    ret = esp_bt_controller_init(&bt_cfg);  // Initialize the BT controller

    if (ret) {
        MY_LOGE(LOG_TAG_Common, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);  // Enable the BT controller in BLE mode
    if (ret) {
        MY_LOGE(LOG_TAG_Common, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();  // Initialize the bluedroid stack
    if (ret) {
        MY_LOGE(LOG_TAG_Common, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();  // Enable the bluedroid stack
    if (ret) {
        MY_LOGE(LOG_TAG_Common, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_ble_gap_register_callback(gap_event_handler);  // Register the gap event handler
    if (ret) {
        MY_LOGE(LOG_TAG_Common, "gap register error, error code = %x", ret);
        return;
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);  // Delay for 200ms to allow the stack to initialize

    ctx->test_sem = xSemaphoreCreateBinary();  // Create a binary semaphore for synchronization

    if (ctx->test_sem == NULL) {
        MY_LOGE(LOG_TAG_Common, "Failed to create test semaphore");
        return;
    }
}

role_transfer_t get_transition(device_role_t from, device_role_t to) {

    if (from == ROLE_PERIPHERAL && to == ROLE_CENTRAL) {

        return TRANS_PRPH_CEN;
    } else if (from == ROLE_CENTRAL && to == ROLE_PERIPHERAL) {

        return TRANS_CEN_PRPH;
    } else if (from == ROLE_PERIPHERAL && to == ROLE_REPEATER) {

        return TRANS_PRPH_REPEATER;
    } else if (from == ROLE_REPEATER && to == ROLE_PERIPHERAL) {

        return TRANS_REPEATER_PRPH;
    }

    else if (from == ROLE_UNDEFINED && to == ROLE_PERIPHERAL) {

        return TRANS_NONE_PRPH;
    }

    else if (from == ROLE_UNDEFINED && to == ROLE_CENTRAL) {

        return TRANS_NONE_CEN;
    }

    return TRANS_NONE;
}

void switch_role(ble_node_context_t *ctx) {

    MY_LOGW(LOG_TAG_Common, "Switching role from %d to %d and last known role is %d ", ctx->my_current_role, ctx->my_new_role, ctx->last_known_role);

    role_transfer_t role_transfer = get_transition(ctx->my_current_role, ctx->my_new_role);

    if (role_transfer == TRANS_NONE) {

        MY_LOGE(LOG_TAG_Common, "No role transition needed. Current role: %d, New role: %d", ctx->my_current_role, ctx->my_new_role);
        return;
    }

    transition_action_t action = transition_table[role_transfer];

    ctx->last_known_role = action.gap_event_handler_stop_role;

    if (action.stop_prph == true) {

        MY_LOGW(LOG_TAG_Peripheral, "(STOP) Role switch to peripheral");
        stop_peripheral_mode(ctx);
    }

    if (action.stop_cen == true) {
        MY_LOGW(LOG_TAG_Central, "(STOP) Role switch to central");
        stop_central_mode(ctx);
    }

    ctx->my_current_role = ctx->my_new_role;  // Update current role after stopping previous role

    if (action.start_prph == true) {

        MY_LOGI(LOG_TAG_Peripheral, COLOR_BRIGHT_BLUE "[FLOW-01] PERIPHERAL_ROLE_SELECTED" COLOR_RESET);
        init_peripheral_context();
        start_peripheral_mode(ctx);
    }

    if (action.start_cen == true) {

        MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-01] CENTRAL_ROLE_SELECTED" COLOR_RESET);
        init_central_context();
        start_central_mode(ctx);
    }
}