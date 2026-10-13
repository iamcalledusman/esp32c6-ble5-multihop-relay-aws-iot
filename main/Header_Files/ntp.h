#ifndef NTP_H
#define NTP_H

#include "ble_node_context.h"
#include "esp_sntp.h"
#include <sys/time.h>
#include <time.h>

void obtain_and_print_ntp_time(void);
void central_schedule_next_discovery_window(ble_node_context_t *ctx);

#endif