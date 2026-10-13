#include "ble_sync_utils.h"
#include "log.h"

void create_periodic_sync(periodic_device_info_t *device) {

    if (!device || device->is_syncing || device->is_synced) {  // check if device is syncing or synced

        MY_LOGW(LOG_TAG_Central, "Device already syncing/synced or invalid");
        return;
    }

    esp_ble_gap_periodic_adv_sync_params_t sync_params = {
        // periodic advertising sync parameters
        .filter_policy = 0,   .sid = device->sid, .addr_type = device->addr_type, .skip = 0,
        .sync_timeout = 6000,  // 60 s → 4× your adv interval is a safe rule‑of‑thumb
    };
    memcpy(sync_params.addr, device->addr, 6);  // copy device address to sync parameters

    esp_err_t ret = esp_ble_gap_periodic_adv_create_sync(&sync_params);  // create periodic sync

    if (ret == ESP_OK) {

        device->is_syncing = true;

        MY_LOGD(LOG_TAG_Central, "[FLOW-32] CENTRAL_CREATE_SYNC_TO_PRPH_PA: MAC=%02X:%02X:%02X:%02X:%02X:%02X, SID=%d", device->addr[0], device->addr[1], device->addr[2], device->addr[3],
                device->addr[4], device->addr[5], device->sid);
    } else {

        MY_LOGE(LOG_TAG_Central, "Failed to create sync (MAC=%02X:%02X:%02X:%02X:%02X:%02X, SID=%d), error=0x%x", device->addr[0], device->addr[1], device->addr[2], device->addr[3], device->addr[4],
                device->addr[5], device->sid, ret);
    }
}