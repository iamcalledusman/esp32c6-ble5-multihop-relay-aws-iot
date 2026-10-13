#include "time_sync.h"
#include "ble_node_context.h"
#include "time.h"
#include <sys/time.h>

void utc_normal_message_action_from_Peripheral(ble_node_context_t *ctx) {

    switch (ctx->utc_time_update.ble_message_type) {

    case BLE_NORMAL_MESSAGE:

        MY_LOGD(LOG_TAG_NTP, "BLE_NORMAL_MESSAGE");

        break;

    case BLE_TIME_REQUEST_MESSAGE:

        MY_LOGD(LOG_TAG_NTP, "BLE_TIME_REQUEST_MESSAGE");

        if (ctx->utc_time_update.ble_time_update == TIME_UPDATED) {

            time_t now;
            struct tm utc_time;

            time(&now);  // Get the current time in seconds since the epoch
            gmtime_r(&now, &utc_time);

            MY_LOGD(LOG_TAG_NTP, "UTC when connected to NTP server ext adv received from peripheral: %04d-%02d-%02d %02d:%02d:%02d", utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday,
                    utc_time.tm_hour, utc_time.tm_min, utc_time.tm_sec);

            // ctx->utc_time_update.ble_time_update = TIME_UPDATED;       // central time updated
            ctx->utc_time_update.utc_seconds = (uint8_t)utc_time.tm_sec;  // only seconds are required to send to peripheral

            ctx->raw_ext_adv_data_2m[EXT_ADV_MESSAGE_TYPE_INDEX] = BLE_TIME_RESPONSE_MESSAGE;        // set the message type to time response
            ctx->raw_ext_adv_data_2m[EXT_ADV_TIME_STATUS_INDEX] = TIME_UPDATED;                      // set the time status to updated
            ctx->raw_ext_adv_data_2m[EXT_ADV_UTC_SECONDS_INDEX] = ctx->utc_time_update.utc_seconds;  // set the UTC seconds

        }

        else {
            ctx->raw_ext_adv_data_2m[EXT_ADV_MESSAGE_TYPE_INDEX] = BLE_TIME_RESPONSE_MESSAGE;
            ctx->raw_ext_adv_data_2m[EXT_ADV_TIME_STATUS_INDEX] = TIME_NONE;
            ctx->raw_ext_adv_data_2m[EXT_ADV_UTC_SECONDS_INDEX] = 0;
        }
        break;

    default:
        break;
    }
}

void utc_normal_message_action_from_Central(ble_node_context_t *ctx) {

    struct timeval tv;

    switch (ctx->utc_time_update.ble_message_type) {

    case BLE_NORMAL_MESSAGE:

        MY_LOGD(LOG_TAG_NTP, "BLE_NORMAL_MESSAGE");

        break;

    case BLE_TIME_RESPONSE_MESSAGE: {

        MY_LOGD(LOG_TAG_NTP, "BLE_TIME_RESPONSE_MESSAGE");

        int64_t uptime_us = esp_timer_get_time();

        // Ensure hardware timer has been running for at least 1-2 seconds post-boot
        if (uptime_us < 1000000) {

            ESP_LOGW("CLK_CHECK", "System still in early boot window (Uptime: %lld ms)", uptime_us / 1000);
            return;
        }

        // if(ctx->utc_time_update.ble_time_update==TIME_NONE){

        gettimeofday(&tv, NULL);

        uint8_t peripheral_sec = tv.tv_sec % 60;
        uint8_t central_sec = ctx->utc_time_update.utc_seconds;

        static uint8_t last_central_sec = 255;

        if (central_sec != last_central_sec) {
            last_central_sec = central_sec;

            if (peripheral_sec != central_sec) {
                tv.tv_sec = tv.tv_sec - (tv.tv_sec % 60) + central_sec;

                tv.tv_usec = 0;

                if (settimeofday(&tv, NULL) != 0) {

                    MY_LOGE(LOG_TAG_NTP, "settimeofday failed");
                }
            }

            ctx->utc_time_update.ble_time_update = TIME_UPDATED;
        }

        break;
    }

    default:

        break;
    }
}
