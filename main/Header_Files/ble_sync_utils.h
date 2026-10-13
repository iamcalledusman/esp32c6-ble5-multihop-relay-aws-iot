#ifndef BLE_SYNC_UTILS_H
#define BLE_SYNC_UTILS_H
#include "ble_comm_struct.h"
#include "ble_node_context.h"

void create_periodic_sync(periodic_device_info_t *device);
bool Cen_Ack_Mac_Extract(ble_node_context_t *ctx, uint8_t *adv_data, uint8_t len, char *type);  // extract the MAC address from the adv data from central device
void init_bluetooth_stack(ble_node_context_t *ctx);
void update_role_and_type(ble_node_context_t *ctx, event_source_t event, uint8_t parent_code);
void initialize_role_on_boot(ble_node_context_t *ctx);
void switch_role(ble_node_context_t *ctx);
// bool wait_for_adv_cmd(ble_node_context_t *ctx, adv_command_t type);

void update_shared_code_and_adv_payload(ble_node_context_t *ctx, const char *reason);
void set_shared_code(ble_node_context_t *ctx, uint8_t code);
uint8_t *get_shared_code(ble_node_context_t *ctx);
void Read_Wifi_Status_at_boot(ble_node_context_t *ctx);
void read_device_inputs(device_type_t *device);

void init_ack_timer(ble_node_context_t *ctx);

role_transfer_t get_transition(device_role_t from, device_role_t to);
extern transition_action_t transition_table[7];
#endif  // BLE_SYNC_UTILS_H