#include "ntp.h"
#include "ble_node_context.h"
#include "common_mode.h"
#include <stdint.h>


// #define LOCAL_NTP_SERVER_IP "192.168.18.130"     // Replace with your PC IPv4

#define LOCAL_NTP_SERVER_IP "192.168.1.67"  // Replace with your PC IPv4

// #define LOCAL_NTP_SERVER_IP "192.168.100.15"   // Replace with your PC IPv4

static void time_sync_notification_cb(struct timeval *tv) { MY_LOGI(LOG_TAG_NTP, COLOR_BRIGHT_MAGENTA "NTP synchronization completed" COLOR_RESET); }

static void start_local_sntp(void) {
    MY_LOGD(LOG_TAG_NTP, "Starting SNTP server: %s", LOCAL_NTP_SERVER_IP);

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_setservername(0, LOCAL_NTP_SERVER_IP);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

static bool wait_for_time_sync(void) {
    for (int retry = 0; retry < 20; retry++) {

        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            return true;
        }

        MY_LOGI(LOG_TAG_NTP, COLOR_BRIGHT_MAGENTA "Waiting for NTP response... %d/20" COLOR_RESET, retry + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return false;
}

void central_schedule_next_discovery_window(ble_node_context_t *ctx) {
    if (ctx == NULL) {

        MY_LOGE(LOG_TAG_NTP, "Invalid BLE node context");
        return;
    }

    time_t now;
    struct tm utc_time;

    time(&now);
    gmtime_r(&now, &utc_time);

    MY_LOGI(LOG_TAG_NTP, COLOR_BRIGHT_MAGENTA "UTC time: %04d-%02d-%02d %02d:%02d:%02d" COLOR_RESET, utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday, utc_time.tm_hour, utc_time.tm_min,
            utc_time.tm_sec);

    uint64_t delay_us = (uint64_t)(60 - utc_time.tm_sec) * 1000000ULL;

    if (!esp_timer_is_active(ctx->central_one_second_timer)) {
        ESP_ERROR_CHECK(esp_timer_start_once(ctx->central_one_second_timer, delay_us));
    }
}
void obtain_and_print_ntp_time(void) {
    ble_node_context_t *ctx = &ble_node_ctx;

    /*
     * Call this only after Wi-Fi is connected
     * and the ESP32 has received an IP address.
     */

    start_local_sntp();

    if (wait_for_time_sync()) {
        ctx->utc_time_update.ble_time_update = TIME_UPDATED;
        central_schedule_next_discovery_window(ctx);

    } else {
        ESP_LOGE(LOG_TAG_NTP, "Failed to obtain time from local NTP server");
    }
}
