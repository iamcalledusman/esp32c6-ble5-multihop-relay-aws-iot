#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_node_context.h"
#include "ble_sync_utils.h"
#include "common_mode.h"
#include "device_type_utils.h"
#include "log.h"

device_type_t look_up_table[max_types] = {

    {usb, wifi_5_6, Not_synced, "A0", 1, 0}, {battery, wifi_6, Not_synced, "B0", 2, 0}, {battery, wifi_5, Not_synced, "C0", 3, 0},
    {usb, wifi_5_6, synced, "A1C", 4, 0},    {battery, wifi_6, synced, "B1C", 5, 0},    {battery, No_wifi, synced, "A2R", 6, 4},
    {battery, wifi_5, synced, "C1C", 7, 0},  {battery, No_wifi, synced, "B2R", 8, 5},   {battery, No_wifi, synced, "C2R", 9, 7},

    /*  Correct Mapping:

            Central Types (A1C, B1C, C1C) → Codes 4, 5, 7

            Repeater Types (A2R, B2R, C2R) → Codes 6, 8, 9

    */

};

void update_shared_code_and_adv_payload(ble_node_context_t *ctx, const char *reason) {

    uint8_t device_code = 0;

    char *device_type = get_type(&ctx->device, &device_code);  // get device code (1-9) from ctx->device.power wifi and sync (ctx->device we entered during init )

    if (device_code == 0 || device_code >= 10) {

        MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_BLUE "[FLOW-007] SKIP update, invalid code=%d reason=%s" COLOR_RESET, device_code, reason);
        if (device_type)
            free(device_type);

        return;
    }

    set_shared_code(ctx, device_code);  // Set the shared code

    memcpy(&ctx->raw_ext_adv_data_2m[32], get_shared_code(ctx), 1);

    MY_LOGI(LOG_TAG_Common, COLOR_BRIGHT_BLUE "[FLOW-05] CENTRAL_SHARED_CODE_UPDATED: code=%d adv[32]=%d reason=%s role=%d" COLOR_RESET, device_code, ctx->raw_ext_adv_data_2m[32], reason,
            ctx->my_current_role);

    if (device_type) {

        free(device_type);  // free the allocated memory
    }
}

void set_shared_code(ble_node_context_t *ctx, uint8_t code) {

    ctx->shared_code = code;  // Set the shared code globally
}

uint8_t *get_shared_code(ble_node_context_t *ctx) {

    return &ctx->shared_code;  // Return the shared code address
}

device_role_t get_role_from_code(uint8_t code) {
    switch (code) {
    case 4:
    case 5:
    case 7:                    // case 1: case 2: case 3:
        return ROLE_REPEATER;  // Synced with Central
                               /*
                                   case 4: case 5: case 7:
                                          return ROLE_PERIPHERAL;  // Synced with Peripheral  , this is just for testing purpose, we will change it later, because when testing bi directional sync , it was setting it
                                  repeater
                                */
    case 6:
    case 8:
    case 9:
        return ROLE_PERIPHERAL;  // Synced with Repeater
    default:
        return ROLE_PERIPHERAL;  // Default or base sync
    }
}

const char *get_type_string_by_code(uint8_t code) {
    for (int i = 0; i < 9; i++) {
        if (look_up_table[i].code == code) {
            return look_up_table[i].type;
        }
    }
    return "UNK";
}

// returns device type and code from the look-up table based on the device's power, wifi, and sync types

char *get_type(device_type_t *device, uint8_t *device_code) {

    int i;
    char *device_type = malloc(5);

    if (device_type == NULL) {

        *device_code = 0;

        return NULL;
    }

    for (i = 0; i < max_types; i++) {

        device_type_t entry = look_up_table[i];

        if (entry.power == device->power && entry.wifi == device->wifi && entry.sync == device->sync && entry.remote_code == device->remote_code) {

            strncpy(device_type, entry.type, 4);
            device_type[4] = '\0';
            *device_code = entry.code;

            return device_type;
        }
    }

    strncpy(device_type, "UNK", 4);
    device_type[4] = '\0';

    *device_code = 0;
    return device_type;
}

// Function to update device role(central/prph/repeater) based on events(boot, ack received, wifi connected/disconnected, sync disconnected)
void update_role_and_type(ble_node_context_t *ctx, event_source_t event, uint8_t parent_code) {

    switch (event) {
    case EVENT_BOOT:
        initialize_role_on_boot(ctx);
        // ctx->my_current_role = ctx->my_new_role;
        MY_LOGD(LOG_TAG_Peripheral, "Boot event -> Initial role set to %d\n", ctx->my_new_role);
        return;

    case EVENT_ACK_RECEIVED:
        ctx->my_new_role = get_role_from_code(parent_code);
        MY_LOGI(LOG_TAG_Peripheral, "ACK received -> Role set to %d\n", ctx->my_new_role);
        break;

    case EVENT_SYNC_DISCONNECTED:
        ctx->my_new_role = ROLE_PERIPHERAL;
        MY_LOGI(LOG_TAG_Peripheral, "Sync lost -> Reverting to PERIPHERAL\n");
        break;

    case EVENT_WIFI_CONNECTED:
        ctx->my_new_role = ROLE_CENTRAL;
        MY_LOGI(LOG_TAG_Peripheral, "Wi-Fi connected -> Acting as Central\n");
        break;

    case EVENT_WIFI_DISCONNECTED:
        ctx->my_new_role = ROLE_PERIPHERAL;
        MY_LOGI(LOG_TAG_Peripheral, "Wi-Fi disconnected -> Acting as Peripheral\n");
        break;

    default:
        MY_LOGW(LOG_TAG_Peripheral, "Unknown event -> No role change\n");
        return;
    }

    const char *type_str = get_type_string_by_code(parent_code);
    MY_LOGI(LOG_TAG_Peripheral, "Device type: %s, code: %d\n", type_str, parent_code);
    // share_type_info(type_str, parent_code); // Uncomment and implement if needed
}

void initialize_role_on_boot(ble_node_context_t *ctx) {
    if (ctx->device.wifi != No_wifi) {
        ctx->my_new_role = ROLE_CENTRAL;
        MY_LOGI(LOG_TAG_Central, COLOR_BRIGHT_BLUE "[FLOW-04] CENTRAL_ROLE_SELECTED" COLOR_RESET);
    } else {
        ctx->my_new_role = ROLE_PERIPHERAL;
        MY_LOGI(LOG_TAG_Peripheral, "Device is acting as peripheral");
    }
}
