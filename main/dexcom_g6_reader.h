#include "esp_log.h"
#include "host/ble_gatt.h"
#include "os/os.h"

// Opcodes
#define AUTH_REQUEST_TX_OPCODE      0x1
#define AUTH_CHALLENGE_RX_OPCODE    0x3
#define AUTH_CHALLENGE_TX_OPCODE    0x4
#define AUTH_STATUS_RX_OPCODE       0x5

#define KEEP_ALIVE_TX_OPCODE        0x6
#define BOND_REQUEST_TX_OPCODE      0x7
#define BOND_REQUEST_RX_OPCODE      0x8

#define DISCONNECT_TX_OPCODE        0x9

#define GLUCOSE_TX_OPCODE           0x4e
#define GLUCOSE_RX_OPCODE           0x4f

extern const char *sensor_id;

extern const ble_uuid16_t advertisement_uuid;
extern const ble_uuid128_t cgm_service_uuid;
extern const ble_uuid128_t control_uuid;
extern const ble_uuid128_t authentication_uuid;

/** mbuf **/
#define MBUF_PKTHDR_OVERHEAD        sizeof(struct os_mbuf_pkthdr)
#define MBUF_MEMBLOCK_OVERHEAD      sizeof(struct os_mbuf) + MBUF_PKTHDR_OVERHEAD
#define MBUF_NUM_MBUFS              32
#define MBUF_PAYLOAD_SIZE           32
#define MBUF_BUF_SIZE               OS_ALIGN(MBUF_PAYLOAD_SIZE, 4)
#define MBUF_MEMBLOCK_SIZE          (MBUF_BUF_SIZE + MBUF_MEMBLOCK_OVERHEAD)
#define MBUF_MEMPOOL_SIZE           OS_MEMPOOL_SIZE(MBUF_NUM_MBUFS, MBUF_MEMBLOCK_SIZE)

struct os_mbuf_pool dgr_mbuf_pool;
struct os_mempool dgr_mbuf_mempool;
os_membuf_t dgr_mbuf_buffer[MBUF_MEMPOOL_SIZE];

/** list for characteristics */
typedef struct list_element {
    struct list_element *next;
    struct ble_gatt_chr data;
} list_element;

typedef struct list {
    list_element *head;
    list_element *tail;
    int length;
} list;

/**  util.c **/
char* addr_to_string(const void *addr);
void print_adv_fields(struct ble_hs_adv_fields *adv_fields);
void dgr_print_rx_packet(struct os_mbuf *om);

/**  gatt.c **/
void dgr_discover_services(uint16_t conn_handle);
void dgr_handle_rx(struct os_mbuf *om, uint16_t attr_handle, uint16_t conn_handle);

/**  characteristics.c */
void dgr_add_to_list(list *l, const struct ble_gatt_chr *in);
int dgr_find_in_list(list *l, const ble_uuid_t *uuid, struct ble_gatt_chr *out);
void dgr_print_list(list *l);

/**  messages.c **/
void dgr_build_auth_request_msg(struct os_mbuf *om);
void dgr_build_auth_challenge_msg(struct os_mbuf *om);
void dgr_build_keep_alive_msg(struct os_mbuf *om, uint8_t time);
void dgr_build_bond_request_msg(struct os_mbuf *om);
void dgr_parse_auth_challenge_msg(uint8_t *data, uint8_t length, bool *correct_token);
void dgr_parse_auth_status_msg(uint8_t *data, uint8_t length);
void dgr_create_mbuf_pool();
void dgr_create_crypto_context();
void dgr_print_token_details();