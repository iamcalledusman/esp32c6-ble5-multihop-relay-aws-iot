#ifndef PERIPHERAL_MODE_H
#define PERIPHERAL_MODE_H

#include "ble_node_context.h"
#include "esp_gap_ble_api.h"

void start_peripheral_mode(ble_node_context_t *ctx);
void stop_peripheral_mode(ble_node_context_t *ctx);
extern esp_ble_gap_ext_adv_params_t ext_adv_params_2M_Peripheral;
extern esp_ble_gap_ext_adv_params_t ext_adv_params_beacon_Peripheral;
extern esp_ble_gap_periodic_adv_params_t periodic_adv_params_peripheral;
void gap_event_handler_Peripheral(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param, ble_node_context_t *ctx);

#endif  // PERIPHERAL_MODE_H