#include "wifi.h"
#include "ble_node_context.h"
#include "ble_sync_utils.h"
#include "mqtt_module.h"
#include "ntp.h"
#include "sensor_module.h"

char retries = 0;

wifi_type_t wifi_type_return;

EventGroupHandle_t wifi_event_group;

TaskHandle_t role_task_handle = NULL;

static void start_retry_timer(ble_node_context_t *ctx);
static void start_Wifi_stability_timer(ble_node_context_t *ctx);

char *wifi_version_strings[4] = {

    "0", "wifi_version_5_6", "wifi_version_6", "wifi_version_5"};

void initialize_wifi() {

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL) {

        MY_LOGE(LOG_TAG_Wifi, "Failed to create wifi_event_group");

        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // First power-saving test
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Configure Wi-Fi credentials
    configure_wifi();

    // Attempt to connect to the Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_connect());
}

void wifi_stability_timer_callback(TimerHandle_t xTimer) {

    // ble_node_context_t *ctx = (ble_node_context_t *)pvTimerGetTimerID(xTimer);

    xEventGroupSetBits(wifi_event_group, WIFI_STABILITY_BIT);

    MY_LOGW(LOG_TAG_Wifi, "Wi-Fi stability timer expired, checking Wi-Fi status...");
}
// 2. Wi-Fi Event Handling
void start_retry_timer(ble_node_context_t *ctx) {

    if (ctx->retry_timer == NULL) {

        ctx->retry_timer = xTimerCreate("retry timer", pdMS_TO_TICKS(4000), pdFALSE, NULL, retry_timer_callback);
    }

    xTimerStart(ctx->retry_timer, 0);
}

void start_Wifi_stability_timer(ble_node_context_t *ctx) {

    if (ctx->wifi_timer_stability_timer == NULL) {

        // ctx->wifi_timer_stability_timer=xTimerCreate("wifi stability timer",pdMS_TO_TICKS(20000),pdFALSE,ctx,wifi_stability_timer_callback);

        ctx->wifi_timer_stability_timer = xTimerCreate("wifi stability timer", pdMS_TO_TICKS(20000), pdFALSE, NULL, wifi_stability_timer_callback);
    }

    xTimerReset(ctx->wifi_timer_stability_timer, 0);
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    ble_node_context_t *ctx = &ble_node_ctx;

    if (event_base == WIFI_EVENT) {

        switch (event_id) {

        case WIFI_EVENT_STA_CONNECTED:

            MY_LOGI(LOG_TAG_Wifi, COLOR_BRIGHT_WHITE "Wi-Fi connected!" COLOR_RESET);

            // ctx->wifi_info.wifi_state = WIFI_CONNECTED_SCANNER_MODE;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

            if (ctx->retry_timer != NULL) {

                xTimerStop(ctx->retry_timer, 0);
            }

            retries = 0;
            break;

        case WIFI_EVENT_STA_DISCONNECTED:

            MY_LOGW(LOG_TAG_Wifi, "Wi-Fi disconnected!");
            // ctx->wifi_info.wifi_state = WIFI_DISCONNECTED_ADVERTISER_MODE;
            //  xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
            start_retry_timer(ctx);

            break;

        default:
            break;
        }

    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        MY_LOGD(LOG_TAG_Wifi, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        if (Wifi_Read_Via_Scan ==
            true) {  // read the wifi or sync code via button press and update the ext adv data at boot time , if the flag is set to true which is set by pressing the button during init

            obtain_and_print_ntp_time();

            update_shared_code_and_adv_payload(ctx, "wifi connected and wifi read mode is SCAN");  // update the shared code and adv payload
        }

        //    mqtt_app_start();   // i will shift this to other place this is just for testing ,,,,,,,,,,
    }
}

// 4. WIFI Initialization

void configure_wifi() {

    wifi_config_t wifi_config = {

        .sta =
            {

           
                .ssid = "YOUR_WIFI_SSID",  // Replace with your Wi-Fi SSID
                .password = "YOUR_WIFI_PASSWORD",     // Replace with your Wi-Fi Password
                .listen_interval = 3,



            },
    };

    // Set Wi-Fi configuration for Station mode
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    if (ret == ESP_OK) {

        MY_LOGD(LOG_TAG_Wifi, "Wi-Fi credentials set successfully.");

    } else {

        MY_LOGE(LOG_TAG_Wifi, "Failed to set Wi-Fi credentials: %d", ret);
    }
}

void retry_timer_callback(TimerHandle_t xTimer) {

    ble_node_context_t *ctx = &ble_node_ctx;

    if (retries < 5) {

        MY_LOGW(LOG_TAG_Wifi, "Wi-Fi disconnected. Reconnecting... %d", retries);
        esp_wifi_connect();
        retries++;

        xTimerChangePeriod(ctx->retry_timer, pdMS_TO_TICKS(1000 << retries), 0);
        xTimerStart(ctx->retry_timer, 0);

    }

    else {

        MY_LOGE(LOG_TAG_Wifi, "Max tries reached .....");
        retries = 0;
    }
}

wifi_type_t get_wifi_AP_info() {

    wifi_ap_record_t ap_info;

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {

        if (ap_info.phy_11ax == 1)
            return wifi_6;  // wifi_6=2,

        else if (ap_info.phy_11ac == 1)
            return wifi_5;  // wifi_5=3

        else {

            MY_LOGI(LOG_TAG_Wifi, COLOR_BRIGHT_WHITE "Older Wifi version ... " COLOR_RESET);

            return wifi_5_6;  // wifi_5_6=1, this means either wifi 4 or 5 but we cannot be sure which one is it so we will just say its wifi_5_6
        }

    }

    else {

        MY_LOGE(LOG_TAG_Wifi, "Failed to access the wifi-AP-Info ");

        return No_wifi;
    }
}

void update_wifi_version(wifi_type_t wifi_Get_type, ble_node_context_t *ctx) {

    if (wifi_Get_type >= No_wifi && wifi_Get_type <= wifi_5) {

        MY_LOGI(LOG_TAG_Wifi, COLOR_BRIGHT_BLACK "Wifi version is , and also updated the context structure %s" COLOR_RESET, wifi_version_strings[wifi_Get_type]);

        ctx->device.wifi = wifi_Get_type;  // update the context structure

    } else {

        MY_LOGE(LOG_TAG_Wifi, "Invalid Wifi version ...... ");
    }
}

void Role_Update_Task(void *pvParameters) {

    //  static char retries=0;
    uint8_t device_code = 0;

    ble_node_context_t *ctx = (ble_node_context_t *)pvParameters;

    device_type_t device_message;

    esp_err_t ret;

    while (1) {

        xQueueReceive(ctx->device_info_queue, &device_message, portMAX_DELAY);

        char *device_type = get_type(&device_message, &device_code);  // Get the device type(A0,C0...) and code(1-9) ,from look_up_table using malloc , so device_type need to be freed after use

        if (device_type) {

            MY_LOGD(LOG_TAG_Wifi, "Device Type is %s , device code is  %d", device_type, device_code);

            MY_LOGD(LOG_TAG_Common, "before update New Role is  %d ,current Role is  %d", ctx->my_new_role, ctx->my_current_role);

            free(device_type);

        }  // 1

        // Do NOT override repeater role

        // This block protects the REPEATER role from Wi-Fi/GPIO based role updates.
        // If the device is already ROLE_REPEATER, do not allow normal Wi-Fi role logic
        // to override it. Only demote when both Central and Peripheral BLE links are lost.

        if (ctx->my_current_role == ROLE_REPEATER) {

            MY_LOGD(LOG_TAG_Common, "central_present: %d, peripheral_present: %d , repeater_role_demote %d", ctx->repeater_role_demote.central_present, ctx->repeater_role_demote.peripheral_present,
                    ctx->is_repeater_role_demoted);

            if (ctx->repeater_role_demote.central_present == false && ctx->repeater_role_demote.peripheral_present == false && ctx->is_repeater_role_demoted == false) {

                MY_LOGW(LOG_TAG_Common, "Both Central and Peripheral roles inactive, demoting from Repeater to Peripheral");

                ctx->is_repeater_role_demoted = true;

                ctx->my_new_role = ROLE_PERIPHERAL;

                goto perform_role_demote;
            }

            MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_BLUE "[FLOW-39] REPEATER_WIFI_GPIO_ROLE_CHANGE_IGNORED" COLOR_RESET);
            goto skip_role_update;
        }

        update_role_and_type(ctx, EVENT_BOOT, device_code);  // update the role and type on boot event, on the basis of device info in ble_node_ctx.device

        if (ctx->my_current_role != ctx->my_new_role) {

            if (ctx->my_new_role == ROLE_CENTRAL) {  // OR Can be use   if(ble_node_ctx.my_new_role == ROLE_CENTRAL){

                MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_WHITE "Device is in CENTRAL role at boot..." COLOR_RESET);

                ret = TLV493D_Init_Auto();  // It should start only once please check if its starting every time then must have to add the flag here .....

                if (ret != ESP_OK) {
                    MY_LOGE(LOG_TAG_Sensor, "TLV493D init failed");
                    continue;
                }

                mqtt_app_start();
            }
        }

    perform_role_demote:

        switch_role(ctx);  // Start BLE operations based on the current role

        //}

    skip_role_update:
    }
}

void Read_Wifi_Status(void *pvParam) {

    ble_node_context_t *ctx = (ble_node_context_t *)pvParam;

    static uint8_t last_wifi = 255;

    while (1) {

        if (Wifi_Read_Via_Buttons) {

            MY_LOGD(LOG_TAG_Wifi, " Wifi Read Via Buttons");

            ctx->device.power = (gpio_get_level(input_pins[0]) == 1) ? battery : usb;       // usb=0 ,battery=1
            ctx->device.sync = (gpio_get_level(input_pins[1]) == 1) ? Not_synced : synced;  // Not_synced=1 , synced=0
            int wifi_value = (gpio_get_level(input_pins[3]) << 1) | gpio_get_level(input_pins[2]);

            switch (wifi_value) {
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

            MY_LOGD(LOG_TAG_Common, "Power : %s  Sync : %s  Wifi :  %s ", get_power_string(ctx->device.power), get_sync_string(ctx->device.sync), get_wifi_string(ctx->device.wifi));
            xQueueSend(ctx->device_info_queue, &ctx->device, portMAX_DELAY);

            vTaskDelay(pdMS_TO_TICKS(5000));

        }

        else if (Wifi_Read_Via_Scan) {

            EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT | WIFI_STABILITY_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

            MY_LOGD(LOG_TAG_Wifi, " Wifi Read Via Scan ");

            ctx->device.power = (gpio_get_level(input_pins[0]) == 1) ? usb : battery;       // usb=0 ,battery=1
            ctx->device.sync = (gpio_get_level(input_pins[1]) == 1) ? Not_synced : synced;  // Not_synced=1 , synced=0

            if ((bits & WIFI_CONNECTED_BIT) == WIFI_CONNECTED_BIT) {

                MY_LOGD(LOG_TAG_Wifi, "Wi-Fi connected, switching to BLE scanner mode.");

                wifi_type_return = get_wifi_AP_info();

                if (wifi_type_return != 0) {  //

                    update_wifi_version(wifi_type_return, ctx);  // Wifi enum type , code added
                    start_Wifi_stability_timer(ctx);             // start the stability timer when connected to wifi

                }

                else {

                    MY_LOGW(LOG_TAG_Wifi, "Invalid/old Wifi version ...... ");
                    ctx->device.wifi = No_wifi;
                }

            } else if ((bits & WIFI_DISCONNECTED_BIT) == WIFI_DISCONNECTED_BIT) {

                MY_LOGD(LOG_TAG_Wifi, "Wi-Fi Dis-connected, switching to BLE advertiser mode.");
                ctx->device.wifi = No_wifi;
                start_Wifi_stability_timer(ctx);  // start the stability timer when disconnected from wifi

            } else if ((bits & WIFI_STABILITY_BIT) == WIFI_STABILITY_BIT) {

                MY_LOGD(LOG_TAG_Common, "Power : %s  Sync : %s  Wifi :  %s ", get_power_string(ctx->device.power), get_sync_string(ctx->device.sync), get_wifi_string(ctx->device.wifi));
                xQueueSend(ctx->device_info_queue, &ctx->device, portMAX_DELAY);
            }
        }
    }
}
