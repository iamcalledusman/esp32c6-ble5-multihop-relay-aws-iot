#include "mqtt_module.h"
#include "sensor_module.h"
#include <esp_wifi.h>

esp_mqtt_client_handle_t client;

uint8_t mac_address[6] = {0};
char packet_str[70];

extern const uint8_t client_cert_pem_start[] asm("_binary_client_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_client_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_client_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_client_key_end");
extern const uint8_t server_cert_pem_start[] asm("_binary_mosquitto_org_crt_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_mosquitto_org_crt_end");

uint8_t eddystone_uid_packet[] = {

    // Eddystone advertisment Packet Format (11 bytes)

    0x02, 0x01, 0x06, 0x03, 0x03, 0xaa, 0xfe,  // constant for all Eddystone advertisement packets
    0x17,                                      // Packet length,23
    0x16,                                      // Service Data Type Value
    0xaa, 0xfe,                                // 16 bits // 2 bytes Eddystone UUID
    // Service Data
    0x00,                                            //(0)     ,(0x08 previously), Should be 0x00 for UID Frame Type
    0x12,                                            //(1)     ,1A Tx PWR dBm , 1B VBATT..........
    0xDE, 0x33,                                      //(2),(3) ,Product ID
    0x01, 0x01, 0x01,                                //(4,5,6) ,STATIC Sensor device ID   ........
    0x0D,                                            //(7)  ,7A FRAME TYPE,7B TX SEQUENCE....
    0x29,                                            //(8) ,sensor 0 value....
    0x67, 0xA1, 0x31, 0x6C, 0x00, 0x00, 0xFF, 0xFF,  //(9-16) MAC address 6 bytes and remining 2 0xFF.....
    0xBB,                                            //(17) ,sensor 1 value
    0xAA,                                            //(18) ,Future Usage
    0xFF                                             //(19) ,CheckSum Bytes 0 to 18. Capture the least significant 8b

};

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        MY_LOGE(LOG_TAG_MQTT, "Last error %s: 0x%x", message, error_code);
    }
}
/*
void publish_sensor_event(const char *event_message) {
    // Publish the message to the defined topic
    int msg_id = esp_mqtt_client_publish(client, SENSOR_EVENT_TOPIC, event_message, 0, 1, 0);
    MY_LOGI(LOG_TAG_MQTT, "Sent publish successful, msg_id=%d", msg_id);
}

// Function to call when the sensor event occurs
void on_sensor_event() {
    char payload[PAYLOAD_BUFFER_SIZE];
    char * Sensor_payload = "Sensor Payload";
    char Value =98;
    // Format the payload, e.g., with sensor data and timestamp
   snprintf(payload, sizeof(payload), "Sensor %s, Value is %d ",Sensor_payload,Value);


    // Publish the sensor event
    publish_sensor_event(payload);
}

// Example function that simulates sensor event detection
void sensor_event_detection_loop() {
    while (1) {
        // Simulate sensor event detection (e.g., a condition check)
     //   if (// sensor event detected //) {
            on_sensor_event();
       // }

        // Delay to prevent busy looping (adjust as needed)
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    MY_LOGD(LOG_TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;       // Casts event_data to esp_mqtt_event_handle_t for easier access.
    esp_mqtt_client_handle_t client = event->client;  // Retrieves the MQTT client handle from the event data
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {  // Handles different MQTT events based on event_id
    case MQTT_EVENT_CONNECTED:
        MY_LOGI(LOG_TAG_MQTT, "MQTT_EVENT_CONNECTED");  // Logs the connection.
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0/usman", 0);
        MY_LOGI(LOG_TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        MY_LOGI(LOG_TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        MY_LOGI(LOG_TAG_MQTT, "sent unsubscribe successful, msg_id=%d", msg_id);  // Subscribes to two topics and then unsubscribes from one.
        break;
    case MQTT_EVENT_DISCONNECTED:
        MY_LOGI(LOG_TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        MY_LOGI(LOG_TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);  // Logs subscription success and publishes a message.
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0/usman", "data from usman", 0, 0, 0);
        MY_LOGI(LOG_TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        MY_LOGI(LOG_TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);  // Logs unsubscription success.
        break;
    case MQTT_EVENT_PUBLISHED:
        MY_LOGI(LOG_TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);  // Logs that a message was published.
        break;
    case MQTT_EVENT_DATA:
        MY_LOGI(LOG_TAG_MQTT, "MQTT_EVENT_DATA");  // Logs received data and prints it.
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        break;
    case MQTT_EVENT_ERROR:
        MY_LOGI(LOG_TAG_MQTT, "MQTT_EVENT_ERROR");  // Logs errors and handles specific TCP transport errors.
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            MY_LOGI(LOG_TAG_MQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        MY_LOGI(LOG_TAG_MQTT, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(void) {
    const esp_mqtt_client_config_t mqtt_cfg = {// Sets up the configuration for the MQTT client, including broker URI and SSL certificates.
                                               .broker.address.uri = "mqtts://xxxxxx-ats.iot.us-east-1.amazonaws.com",  //"mqtts://test.mosquitto.org",//"mqtts://test.mosquitto.org:8884",
                                               .broker.address.port = 8883,
                                               .broker.verification.certificate = (const char *)server_cert_pem_start,
                                               .credentials.client_id = "esp32c6_thing_device1",
                                               .credentials = {
                                                   .authentication =
                                                       {
                                                           .certificate = (const char *)client_cert_pem_start,
                                                           .key = (const char *)client_key_pem_start,
                                                       },
                                               }};

    //  ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());   //Logs the available free memory.
    client = esp_mqtt_client_init(&mqtt_cfg);  // Initializes the MQTT client with the specified configuration.

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);  // Starts the MQTT client, enabling it to connect to the broker and handle events.
}

//                               MQTT

//.................................. MQTT_Message Start...................................................

void MQTT_Message(unsigned char M_Type, float X_Value_F, float Y_Value_F, float X_Value_I, float Y_Value_I) {

    static int tx_inc = 0;
    int i = 0, len = 0;

    double Angle_Input;

    eddystone_uid_packet[12] = (0xFF & 0x0F) << 4;  //(1A) Tx PWR dBm , pending ....

    tx_inc++;  // increment  Tx sequence Number

    if (tx_inc > 15)  // range is 0 to 15 and then reset to 0
        tx_inc = 1;

    eddystone_uid_packet[12] = eddystone_uid_packet[12] | 0x0C;  // (1B) 2nd byte , Battery Voltage

    // Battery Voltage is Pending , need to know how to read that value
    // Lower 8 Bits    0....7
    // advData[15]   = (uint8)(X_Value_I >> 8u)<<4 | (uint8)(Y_Value_I >> 8u) ;     //8...11    X0,  8...11    Y0
    if (M_Type == 0xA0) {  // if periodic  Message

        eddystone_uid_packet[18] = 0xA << 4;  //(7A) 7th byte ,FRAME TYPE

        eddystone_uid_packet[18] |= (tx_inc & 0x0F);  // (7B) 7th byte ,TX SEQUENCE
        // Raw_Avg_Data_To_Angle(X_Value_F , Y_Value_F);
        eddystone_uid_packet[28] = 0xAA;  // 17th byte , initial tank level at the time of Loading event / AA ,sensor 1 value

        Raw_Avg_Data_To_Angle(X_Value_F, Y_Value_F, &Angle_Input);
        eddystone_uid_packet[19] = Angle_to_Percentage(Angle_Input);  // 8th Byte ,sensor 0 value

    }

    else if (M_Type == 0xB0) {  // IF LEC message

        eddystone_uid_packet[18] = 0xB << 4;  //(7A) 7th byte ,FRAME TYPE

        eddystone_uid_packet[18] |= (tx_inc & 0x0F);  // (7B) 7th byte ,TX SEQUENCE

        Raw_Avg_Data_To_Angle(X_Value_I, Y_Value_I, &Angle_Input);
        eddystone_uid_packet[28] = Angle_to_Percentage(Angle_Input);  // 17th byte , initial tank level at the time of Loading event / AA ,sensor 1 value

        Raw_Avg_Data_To_Angle(X_Value_F, Y_Value_F, &Angle_Input);
        eddystone_uid_packet[19] = Angle_to_Percentage(Angle_Input);  // 8th Byte ,sensor 0 value
    }

    esp_wifi_get_mac(WIFI_IF_STA, mac_address);

    eddystone_uid_packet[20] = mac_address[0];  //(9)
    eddystone_uid_packet[21] = mac_address[1];  //(10)
    eddystone_uid_packet[22] = mac_address[2];  //(11)
    eddystone_uid_packet[23] = mac_address[3];  //(12)
    eddystone_uid_packet[24] = mac_address[4];  //(13)
    eddystone_uid_packet[25] = mac_address[5];  //(14)

    // calculate the checksum and add it into the packet

    uint16_t crc = esp_crc16_be(0x1021, eddystone_uid_packet, sizeof(eddystone_uid_packet));

    eddystone_uid_packet[30] = (uint8_t)crc & 0x00FF;

    len = 0;

    for (i = 0; i < sizeof(eddystone_uid_packet); i++) {
        len += snprintf(packet_str + len, sizeof(packet_str) - len, "%02x", eddystone_uid_packet[i]);

        if (len >= sizeof(packet_str)) {
            // Handle buffer overflow if needed
            MY_LOGE(LOG_TAG_MQTT, "Buffer overflow detected");
            return;
        }
    }

    // json format for AWS MQTT Packet

    /*  cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "message", packet_str);


     char *json_string = cJSON_PrintUnformatted(root);

         if (json_string == NULL) {
         MY_LOGE(LOG_TAG_MQTT, "Failed to create JSON string");
         cJSON_Delete(root);
         return;
     }

     MY_LOGI(LOG_TAG_MQTT, "JSON String: %s", json_string);

     if (client == NULL) {
         MY_LOGE(LOG_TAG_MQTT, "MQTT client not ready");
         return;
     }

   int msg_id = esp_mqtt_client_publish(client, "eddystone/uid",json_string, 0, 1, 0);

         if (msg_id != -1) {
             MY_LOGI(LOG_TAG_MQTT, "Published Eddystone UID packet with msg_id=%d", msg_id);
         } else {
             MY_LOGE(LOG_TAG_MQTT, "Failed to publish Eddystone UID packet");
         }

         // Free memory
         cJSON_Delete(root);
         free(json_string);    */
}
