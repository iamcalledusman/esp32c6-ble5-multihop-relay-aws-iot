#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdint.h>

#define EXT_ADV_MESSAGE_TYPE_INDEX 33
#define EXT_ADV_TIME_STATUS_INDEX 34
#define EXT_ADV_UTC_SECONDS_INDEX 35

typedef enum {

    BLE_NORMAL_MESSAGE = 0,
    BLE_TIME_REQUEST_MESSAGE,
    BLE_TIME_RESPONSE_MESSAGE,

} ble_message_type_t;

typedef enum {

    TIME_NONE = 0,
    TIME_UPDATED,
} ble_time_update_t;

typedef struct {
    ble_message_type_t ble_message_type;
    ble_time_update_t ble_time_update;
    uint8_t utc_seconds;

} utc_time_update_t;

#endif
