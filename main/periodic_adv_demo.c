

#include "ble_node_context.h"
#include "ble_sync_utils.h"
#include "central_mode.h"
#include "common_mode.h"
#include "esp_pm.h"
#include "peripheral_mode.h"
#include "sensor_module.h"
#include "wifi.h"
#include <device_type_utils.h>

static void enable_auto_light_sleep(void) {
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };

    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_LOGW("POWER", "Automatic Light-sleep ENABLED");
}

ble_node_context_t ble_node_ctx = {0};  // Initialize the global BLE node context structure

void app_main(void) {

    uint8_t device_code = 0;

    esp_err_t ret;
    // peipheral role

    // First: keep the whole framework quiet/stable
    esp_log_level_set("*", ESP_LOG_INFO);

    // Then enable DEBUG only for YOUR tags
    /*      esp_log_level_set(LOG_TAG_Common,     ESP_LOG_DEBUG);
         esp_log_level_set(LOG_TAG_Central,    ESP_LOG_DEBUG);
         esp_log_level_set(LOG_TAG_Peripheral, ESP_LOG_DEBUG);
         esp_log_level_set(LOG_TAG_Sensor,     ESP_LOG_DEBUG);
         esp_log_level_set(LOG_TAG_NTP,        ESP_LOG_DEBUG);  */

    // Keep noisy libraries restricted
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("mqtt_client", ESP_LOG_ERROR);
    esp_log_level_set("transport_base", ESP_LOG_ERROR);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_ERROR);

    init_ble_node_context();  // Initialize the BLE node context
    init_gpio_inputs();
    ble_get_mac_address(ble_node_ctx.mac_addr);

    ble_node_ctx.adv_queue = xQueueCreate(10, sizeof(adv_msg_t));  // Queue to hold advertising messages for central role and peripheral role

    ble_node_ctx.device_info_queue = xQueueCreate(10, sizeof(device_type_t));  // Queue to hold advertising messages between Role_Update_Task & Read_Wifi_Status

    if (ble_node_ctx.adv_queue == NULL) {

        MY_LOGE(LOG_TAG_Common, "adv_queue creation failed");
        return;
    }

    // init_ack_timer(&ble_node_ctx);                                                // Initialize the ACK timer for extended advertising
    ble_node_ctx.SID = (ble_node_ctx.mac_addr[5] ^ ble_node_ctx.mac_addr[4] ^ ble_node_ctx.mac_addr[3] ^ ble_node_ctx.mac_addr[2] ^ ble_node_ctx.mac_addr[1] ^ ble_node_ctx.mac_addr[0]) &
                       0x0F;  // XOR the MAC address to get the last byte

    MY_LOGI(LOG_TAG_Common, "Generated SID for both the central & peripheral: %d", ble_node_ctx.SID);  // Print the generated SID

    ext_adv_params_2M_Peripheral.sid = ble_node_ctx.SID;  // Set the SID in the extended advertising parameters

    ext_adv_params_2M_Central.sid = ble_node_ctx.SID;

    init_bluetooth_stack(&ble_node_ctx);  // Initialize the Bluetooth stack

    enable_auto_light_sleep();  // <-- ADD THIS

    initialize_wifi();

    Central_One_Second_timer(&ble_node_ctx);          // Initialize the advertising timer for central role
    Central_Three_Seconds_timer_SCAN(&ble_node_ctx);  // Initialize the advertising timer for central role

    /*  Dont delete! in central or peripheral stop funcations , this task will take when run in repeater mode ,
    already in prph mode now when switch to cental need this task  */

    xTaskCreate(Role_Update_Task, "Role Update Task", 4096, &ble_node_ctx, 5, &role_task_handle);

    xTaskCreate(deferredCentralStartTask, "deferred_central_task", 4096, &ble_node_ctx, 5, &deferredCentralStartTaskHandle);

    xTaskCreate(Read_Wifi_Status, "ReadWifiStatus", 4096, &ble_node_ctx, 4, NULL);  // dont delete in central or peripheral stop funcations

    xTaskCreate(adv_command_executor_task, "adv_command_executor_task", 4096, &ble_node_ctx, 8,
                &ble_node_ctx.adv_command_executortask_handle);  // dont delete in central or peripheral stop funcations
}