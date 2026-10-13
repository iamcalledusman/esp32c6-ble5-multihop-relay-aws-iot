#include "ble_node_context.h"
#include "central_mode.h"
#include "common_mode.h"
#include "peripheral_mode.h"


void start_repeater_mode(ble_node_context_t *ctx) {
    ctx->my_current_role = ROLE_REPEATER;  // ✅ Set current role at the start
    start_central_mode(ctx);               // enable scanning, sync
    //    start_peripheral_mode();  // enable periodic+ext adv , already called , as currently it was in peripheral mode
}