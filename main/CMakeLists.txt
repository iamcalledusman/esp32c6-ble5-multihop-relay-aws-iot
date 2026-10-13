set(srcs "periodic_adv_demo.c")


idf_component_register(SRCS "Source_Files/central.c" "Source_Files/common.c" "Source_Files/peripheral.c" "Source_Files/repeater.c" "Source_Files/ble_sync_utils.c"  "Source_Files/ble_comm_struct.c" "Source_Files/device_type_utils.c" 
"Source_Files/ble_node_context.c" "Source_Files/sensor_module.c" "Source_Files/wifi.c" "Source_Files/mqtt_module.c"  "Source_Files/ntp.c" "Source_Files/time_sync.c" "${srcs}"
                       INCLUDE_DIRS "Header_Files"
                       EMBED_FILES "client.crt" "client.key" "mosquitto.org.crt")
