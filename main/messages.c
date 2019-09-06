#include <host/ble_uuid.h>

// Opcodes
#define AUTH_REQUEST_TX_OPCODE      0x1;
#define AUTH_CHALLENGE_RX_OPCODE    0x3;
#define AUTH_CHALLENGE_TX_OPCODE    0x4;
#define AUTH_STATUS_RX_OPCODE       0x5;

#define KEEP_ALIVE_TX_OPCODE        0x6;
#define BOND_REQUEST_TX_OPCODE      0x7;
#define BOND_REQUEST_RX_OPCODE      0x8;

#define DISCONNECT_TX_OPCODE        0x9;

#define GLUCOSE_TX_OPCODE           0x4e;
#define GLUCOSE_RX_OPCODE           0x4f;

// Transmitter services
// FEBC
const ble_uuid16_t advertisement_uuid = BLE_UUID16_INIT(0xfebc);
// F8083532-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t cgm_service_uuid = BLE_UUID128_INIT(0xf8, 0x08, 0x35,
        0x32, 0x84, 0x9e, 0x53, 0x1c, 0xc5, 0x94, 0x30, 0xf1, 0xf8, 0x6a, 0x4e, 0xa5);

// CGM Service characteristics
// F8083534-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t control_uuid = BLE_UUID128_INIT(0xf8, 0x08, 0x35,
        0x34, 0x84, 0x9e, 0x53, 0x1c, 0xc5, 0x94, 0x30, 0xf1, 0xf8, 0x6a, 0x4e, 0xa5);
// F8083535-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t authentication_uuid = BLE_UUID128_INIT(0xf8, 0x08, 0x35,
        0x35, 0x84, 0x9e, 0x53, 0x1c, 0xc5, 0x94, 0x30, 0xf1, 0xf8, 0x6a, 0x4e, 0xa5);