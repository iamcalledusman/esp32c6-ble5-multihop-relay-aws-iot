

#ifndef CENTRAL_MODE_H
#define CENTRAL_MODE_H

#include "ble_node_context.h"
#include "common_mode.h"


void gap_event_handler_Central(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param, ble_node_context_t *ctx);
void start_central_mode(ble_node_context_t *ctx);
void stop_central_mode(ble_node_context_t *ctx);
void Central_One_Second_timer(ble_node_context_t *ctx);
void Central_Three_Seconds_timer_SCAN(ble_node_context_t *ctx);

extern esp_ble_gap_ext_adv_params_t ext_adv_params_2M_Central;
extern esp_ble_gap_ext_adv_params_t ext_adv_params_beacon_Central;
extern esp_ble_gap_periodic_adv_params_t periodic_adv_params_central;

// #define CENTRAL_MAX_PERIODIC_DEVICES  3// maximum number of devices to track

#endif  // CENTRAL_MODE_H