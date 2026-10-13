#ifndef DEVICE_TYPE_UTILS_H
#define DEVICE_TYPE_UTILS_H

#include "common_mode.h"

#define max_types 9

typedef enum { usb = 0, battery = 1 } power_type_t;

typedef enum { No_wifi = 0, wifi_5_6 = 1, wifi_6 = 2, wifi_5 = 3 } wifi_type_t;

typedef enum { synced = 0, Not_synced = 1 } sync_type_t;

typedef struct {
    power_type_t power;
    wifi_type_t wifi;
    sync_type_t sync;
    char type[5];
    uint8_t code;
    uint8_t remote_code;  // code received from the parent device
} device_type_t;

typedef enum { ROLE_UNDEFINED = 0, ROLE_CENTRAL, ROLE_PERIPHERAL, ROLE_REPEATER } device_role_t;

typedef enum { EVENT_BOOT, EVENT_ACK_RECEIVED, EVENT_WIFI_CONNECTED, EVENT_WIFI_DISCONNECTED, EVENT_SYNC_DISCONNECTED } event_source_t;

typedef enum {
    TRANS_PRPH_CEN = 0,
    TRANS_CEN_PRPH,
    TRANS_PRPH_REPEATER,
    TRANS_REPEATER_PRPH,
    TRANS_NONE_PRPH,
    TRANS_NONE_CEN,
    TRANS_NONE,
} role_transfer_t;

typedef struct {

    bool start_prph;
    bool start_cen;
    device_role_t gap_event_handler_stop_role;
    bool stop_prph;
    bool stop_cen;

} transition_action_t;

char *get_type(device_type_t *device, uint8_t *device_code);

const char *get_type_string_by_code(uint8_t code);
device_role_t get_role_from_code(uint8_t code);
void select_device_role(device_type_t *device);

char *get_power_string(power_type_t power);
char *get_sync_string(sync_type_t sync);
char *get_wifi_string(wifi_type_t wifi);

#endif  // DEVICE_TYPE_UTILS_H
