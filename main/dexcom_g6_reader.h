#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "os/os.h"
#include "sys/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include <sys/time.h>

#define SLEEP_BETWEEN_READINGS      600 // in seconds (240)
#define SLEEP_AFTER_ERROR           30 // in seconds

// values for the calibration state
#define CALIB_STATE_STOPPED                     0x01
#define CALIB_STATE_WARMUP                      0x02
#define CALIB_STATE_OK                          0x06
#define CALIB_STATE_NEED_CALIBRATION            0x07
#define CALIB_STATE_NEED_FIRST_CALIBRATION      0x04
#define CALIB_STATE_NEED_SECOND_CALIBRATION     0x05
#define CALIB_STATE_SENSOR_FAILED               0x0b

// values for the transmitter state
#define TRANSMITTER_STATE_OK                    0x0
#define TRANSMITTER_STATE_BATT_LOW              0x81
#define TRANSMITTER_STATE_BRICKED               0x83

// Opcodes
#define AUTH_REQUEST_TX_OPCODE                  0x1
#define AUTH_CHALLENGE_RX_OPCODE                0x3
#define AUTH_CHALLENGE_TX_OPCODE                0x4
#define AUTH_STATUS_RX_OPCODE                   0x5
#define KEEP_ALIVE_TX_OPCODE                    0x6
#define BOND_REQUEST_TX_OPCODE                  0x7
#define BOND_REQUEST_RX_OPCODE                  0x8
#define DISCONNECT_TX_OPCODE                    0x9
#define TIME_TX_OPCODE                          0x24
#define TIME_RX_OPCODE                          0x25
#define GLUCOSE_TX_OPCODE                       0x4e
#define GLUCOSE_RX_OPCODE                       0x4f
#define BACKFILL_TX_OPCODE                      0x50
#define BACKFILL_RX_OPCODE                      0x51

extern const char *transmitter_id;

extern const ble_uuid16_t advertisement_uuid;
extern const ble_uuid128_t cgm_service_uuid;
extern const ble_uuid128_t control_uuid;
extern const ble_uuid128_t authentication_uuid;
extern const ble_uuid128_t backfill_uuid;

extern bool expecting_backfill;

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

/** list for services/characteristics/descriptors */
typedef enum {
    chr_lst_elm,
    dsc_lst_elm,
    svc_lst_elm
} list_elm_type;

typedef struct list_elm {
    list_elm_type type;
    struct list_elm *next;
    union {
        struct ble_gatt_chr chr;
        struct ble_gatt_dsc dsc;
        struct ble_gatt_svc svc;
    };
} list_elm;

typedef struct list {
    list_elm *head;
    list_elm *tail;
    int length;
} list;

list services;
list characteristics;
list descriptors;

/** main.c**/
void dgr_error();
bool dgr_check_bond_state(uint16_t conn_handle);

/** storage.c **/
extern uint32_t last_sequence;
void dgr_init_ringbuffer();
void dgr_save_to_ringbuffer(uint32_t timestamp, uint16_t glucose, uint8_t calibration_state, uint8_t trend);
void dgr_check_for_backfill_and_sleep(uint16_t conn_handle, uint32_t sequence);
void dgr_parse_backfill();
void dgr_print_rbuf(bool keep_items);

/**  util.c **/
char* addr_to_string(const void *addr);
void print_adv_fields(struct ble_hs_adv_fields *adv_fields);
void dgr_print_rx_packet(struct os_mbuf *om);
void dgr_print_conn_sec_state(struct ble_gap_sec_state conn_sec);
uint32_t make_u32_from_bytes_le(const uint8_t *bytes);
uint16_t make_u16_from_bytes_le(const uint8_t *bytes);
void write_u32_le(uint8_t *bytes, uint32_t in);
void write_u16_le(uint8_t *bytes, uint32_t in);
char* translate_transmitter_state(uint8_t state);
char* translate_calibration_state(uint8_t state);

/**  gatt.c **/
void dgr_discover_services(uint16_t conn_handle);
void dgr_handle_rx(struct os_mbuf *om, uint16_t attr_handle, uint16_t conn_handle);
void dgr_send_glucose_tx_msg(uint16_t conn_handle);
void dgr_send_auth_challenge_msg(uint16_t conn_handle);
void dgr_send_keep_alive_msg(uint16_t conn_handle, uint8_t time);

// callbacks
// ble_gatt_dsc_fn
int
dgr_discover_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg);
// ble_gatt_disc_svc_fn
int dgr_discover_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service, void *arg);

// ble_gatt_chr_fn
int dgr_discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_chr *chr, void *arg);

// ble_gatt_attr_fn
int dgr_write_attr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_auth_request_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_read_auth_challenge_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_auth_challenge_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_read_auth_status_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_keep_alive_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_bond_request_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_glucose_tx_msg_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_control_enable_notif_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_time_tx_msg_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_backfill_tx_msg_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);
int dgr_send_backfill_enable_notif_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);

/**  gatt_lists.c */
void dgr_clear_list(list *l);
list_elm* dgr_create_svc_list_elm(struct ble_gatt_svc svc);
list_elm* dgr_create_dsc_list_elm(struct ble_gatt_dsc dsc);
list_elm* dgr_create_chr_list_elm(struct ble_gatt_chr chr);
void dgr_add_to_list(list *l, list_elm *le);
int dgr_find_chr_by_uuid(const ble_uuid_t *uuid, struct ble_gatt_chr *out);
void dgr_print_list(list *l);
void dgr_print_list_elm(list_elm *le);

/**  messages.c **/
extern uint8_t backfill_buffer[500];
extern uint32_t backfill_buffer_pos;
void dgr_enable_server_side_updates_msg(uint16_t conn_handle, const ble_uuid_t *uuid,
                                        ble_gatt_attr_fn *cb, uint8_t type);
void dgr_build_auth_request_msg(struct os_mbuf *om);
void dgr_build_auth_challenge_msg(struct os_mbuf *om);
void dgr_build_keep_alive_msg(struct os_mbuf *om, uint8_t time);
void dgr_build_bond_request_msg(struct os_mbuf *om);
void dgr_build_glucose_tx_msg(struct os_mbuf *om);
void dgr_build_backfill_tx_msg(struct os_mbuf *om);
void dgr_build_time_tx_msg(struct os_mbuf *om);
void dgr_parse_auth_challenge_msg(const uint8_t *data, uint8_t length, bool *correct_token);
void dgr_parse_auth_status_msg(const uint8_t *data, uint8_t length);
void dgr_parse_glucose_msg(const uint8_t *data, uint8_t length, uint8_t conn_handle);
void dgr_parse_backfill_status_msg(const uint8_t *data, uint8_t length);
void dgr_parse_backfill_data_msg(const uint8_t *data, uint8_t length);
void dgr_parse_time_msg(const uint8_t *data, uint8_t length, uint16_t conn_handle);
void dgr_create_mbuf_pool();
void dgr_create_crypto_context();
void dgr_print_token_details();