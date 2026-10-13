#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include "cJSON.h"
#include "esp_crc.h"
#include "freertos/FreeRTOS.h"
#include "log.h"
#include <mqtt_client.h>
#include <stdint.h>

#define LOG_TAG_MQTT "MQTT_MODULE"
#define SENSOR_EVENT_TOPIC "sensor/event"
#define PAYLOAD_BUFFER_SIZE 100

// funcations

void mqtt_app_start(void);
void MQTT_Message(unsigned char M_Type, float X_Value_F, float Y_Value_F, float X_Value_I, float Y_Value_I);

#endif