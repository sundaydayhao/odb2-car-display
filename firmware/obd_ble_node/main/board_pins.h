#pragma once
/* Rev A: ESP32-C3FH4X. Keep synchronized with hardware/obd-ble-node/design.json. */
#define NODE_PIN_BATTERY_ADC 0
#define NODE_PIN_CAN_TX 1
#define NODE_PIN_CAN_RX 3
#define NODE_PIN_TX_INHIBIT 4 /* HIGH forces the physical transceiver TXD recessive. */
#define NODE_PIN_CAN_STANDBY 5
#define NODE_PIN_LED 6
#define NODE_PIN_BOOT 9
#define NODE_BATTERY_DIVIDER_NUM 1100 /* 1 MOhm upper / 100 kOhm lower. */
#define NODE_BATTERY_DIVIDER_DEN 100
