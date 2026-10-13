# ESP32-C6 Periodic Advertising Example

This example demonstrates the **Periodic Advertising** feature on the ESP32-C6 using the ESP-IDF framework. Periodic Advertising allows devices to broadcast data at regular intervals without requiring a connection, making it ideal for efficient and scalable IoT applications.

---

## Overview

This example showcases:
- **Extended Advertising Setup**:
  - Non-connectable, non-scannable advertising for initial synchronization with central devices.
- **Periodic Advertising**:
  - Continuous broadcasting of periodic advertising PDUs after synchronization.
- **Stopping Extended Advertising**:
  - Demonstrates stopping **Extended Advertising** after synchronization to improve energy efficiency.
- **Bluetooth 5.0 Support**:
  - Leverages BLE 5.0 periodic advertising features.

For the synchronization example, refer to the repository: [Periodic Sync Example](https://github.com/iamcalledusman/periodic_sync/tree/master)

---

## Features

1. **Bluetooth 5.0 Periodic Advertising**:
   - Broadcasts periodic AUX_SYNC_IND packets at fixed intervals.
   
2. **Stopping Extended Advertising**:
   - After the central device syncs with periodic advertising, **Extended Advertising** is stopped while periodic advertising continues.

3. **Configurable Advertising Intervals**:
   - **Extended Advertising Interval**: 500 ms
   - **Periodic Advertising Interval**: 5 seconds

4. **Energy Efficiency**:
   - Conserves power by stopping Extended Advertising once synchronization is complete.
5. **Mutli Sync Info Feature**:
   - Mutli sync info with 2 devices , 1st prph syncinfo 5 secs , syncinfo 10 secs .working fine, one limitation is that , when first prph synced then it sync the other. Also need to change the SID and mac address if not then this code will not work.Foe example for 1st prph SID should be 1 and for second it should be 2.Similary i changed the mac address last byte for both

5. **Wire Shark Image Detail**:   
   - Total 8 extended adv PDU + 3*8 primary PDU
   - After that packet # 36 is, synced ack from central, along with primary PDU, The remaining messages are the ack of the PDU from the central side, prph sending only a single periodic PDU every 60 secs to central.
    
6. **Central and Prph side log detail**:   
   - Sync message from central, after that it updates the periodic pdu at firmware end after every 5 sec (ADV SIDE)
   - Receiving periodic PDU every 60 secs, Ext adv PDU stopped (SYNC SIDE)

7. **Generic Firmware(All-in-One) with ACK**:   
   - Single Firware is supposed to work for all (central , prph & repeater) 
   - At startup ,we have to set each role in void init_bluetooth_stack(void) 
   - Inside the above function, manually set the role_status.wifi_enabled = true, for the central role. Compile and load the code  
   - For repeater role_status.sync_established = true.Compile and load the code  
   - For peripheral, have to false above two struct fields as discussed above and as well as this one role_status.periodic_acked = false. 
   - For peripheral we have to load the periodic_adv FW , which is saved in repo, and as mentioned in the Diary
   - Limitaion is that , if we dont load the above code for the prph then prph also act as a repeater .Means multi hop reapter is generated.
   - This repo has the same features of this one https://github.com/iamcalledusman/periodic_adv/tree/prph_multi_sync_info
   - Next plan is to auto detect the device type (A0,B0 , A1C ,A2R ....) 
   
---

## Configuration

### Extended Advertising Parameters
Extended advertising is used for initial synchronization:
```c
esp_ble_gap_ext_adv_params_t ext_adv_params_2M = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
    .interval_min = 800, // 500 ms
    .interval_max = 800,
    .primary_phy = ESP_BLE_GAP_PHY_CODED,
    .secondary_phy = ESP_BLE_GAP_PHY_CODED,
};
