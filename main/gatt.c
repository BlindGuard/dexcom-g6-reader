#include <host/ble_hs.h>
#include "dexcom_g6_reader.h"

const char* tag_gatt = "[Dexcom-G6-Reader][gatt]";
// Transmitter services
// FEBC
const ble_uuid16_t advertisement_uuid = BLE_UUID16_INIT(0xfebc);
// F8083532-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t cgm_service_uuid =
    BLE_UUID128_INIT(0xa5, 0x4e, 0x6a, 0xf8, 0xf1, 0x30, 0x94, 0xc5,
                     0x1c, 0x53, 0x9e, 0x84, 0x32, 0x35, 0x08, 0xf8);

// CGM Service characteristics
// F8083534-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t control_uuid =
    BLE_UUID128_INIT(0xa5, 0x4e, 0x6a, 0xf8, 0xf1, 0x30, 0x94, 0xc5,
                     0x1c, 0x53, 0x9e, 0x84, 0x34, 0x35, 0x08, 0xf8);
// F8083535-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t authentication_uuid =
    BLE_UUID128_INIT(0xa5, 0x4e, 0x6a, 0xf8, 0xf1, 0x30, 0x94, 0xc5,
                     0x1c, 0x53, 0x9e, 0x84, 0x35, 0x35, 0x08, 0xf8);

//list characteristics = {.head = NULL, .length = 0};
list services = {NULL, NULL, 0};
list characteristics = {NULL, NULL, 0};
list descriptors = {NULL, NULL, 0};

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
int dgr_send_notification_enable_msg_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr, void *arg);

void
dgr_send_auth_challenge_msg(uint16_t conn_handle);
void
dgr_send_keep_alive_msg(uint16_t conn_handle, uint8_t time);

void
dgr_discover_characteristics(uint16_t conn_handle) {
    int rc = ble_gattc_disc_all_chrs(conn_handle, 1, 65535, dgr_discover_chr_cb, NULL);

    if (rc != 0) {
        ESP_LOGE(tag_gatt, "Error calling characteristics discovery. rc = 0x%x", rc);
    }
}

void
dgr_discover_descriptors(uint16_t conn_handle) {
    int rc = ble_gattc_disc_all_dscs(conn_handle, 1, 65535, dgr_discover_dsc_cb, NULL);

    if(rc != 0) {
        ESP_LOGE(tag_gatt, "Error calling descriptor discovery. rc = 0x%x", rc);
    }
}

void
dgr_discover_services(uint16_t conn_handle) {
    //rc = ble_gattc_disc_all_chrs(conn_handle, 1, 65535, dgr_discover_chr_cb, NULL);
    int rc = ble_gattc_disc_all_svcs(conn_handle, dgr_discover_service_cb, NULL);

    if (rc != 0) {
        ESP_LOGE(tag_gatt, "Error calling service discovery. rc = 0x%x", rc);
    }
}

void
dgr_handle_rx(struct os_mbuf *om, uint16_t attr_handle, uint16_t conn_handle) {
    if(om && om->om_len > 0) {
        uint8_t op = om->om_data[0];

        dgr_print_rx_packet(om);

        switch(op) {
            case AUTH_CHALLENGE_RX_OPCODE: {
                ESP_LOGE(tag_gatt, "handle_rx: AuthChallenge received.");
                break;
            }

            case AUTH_STATUS_RX_OPCODE: {
                ESP_LOGE(tag_gatt, "handle_rx: AuthStatus received.");
                break;
            }

            case GLUCOSE_RX_OPCODE: {
                ESP_LOGI(tag_gatt, "Received glucose message.");

                dgr_parse_glucose_msg(om->om_data, om->om_len);
                break;
            }

            default:
                ESP_LOGE(tag_gatt, "Unhandled message with opcode = %02x", op);
                break;
        }
    } else {
        ESP_LOGE(tag_gatt, "Received message buffer empty.");
    }
}


/*****************************************************************************
 * message sending                                                           *
 *****************************************************************************/
void
dgr_send_notification_enable_msg(uint16_t conn_handle) {
    // enable notifications by writing two bytes (1, 0) to
    // the CCCD of control_uuid)

    // indication
    uint8_t data[2] = {1, 0};
    // notification
    //uint8_t data[2] = {0, 1};
    struct ble_gatt_chr uh;
    uint16_t handle = 0;
    int rc;

    dgr_find_chr_by_uuid(&characteristics, &control_uuid.u, &uh);
    if(uh.val_handle != 0) {
        handle = uh.val_handle + 1; // cccd lies directly after the corresponding characteristic

        ESP_LOGI(tag_gatt, "[07] Enabling notifications.");
        rc = ble_gattc_write_flat(conn_handle, handle, data, sizeof data,
            dgr_send_notification_enable_msg_cb, NULL);
        if (rc != 0) {
            ESP_LOGE(tag_gatt, "Error while enabling notifications. handle = %d, rc = %d",
                     handle, rc);
        }
    } else {
        ESP_LOGE(tag_gatt, "Could not find val_handle for control uuid.");
    }
}

void
dgr_write_auth_char(uint16_t conn_handle, ble_gatt_attr_fn *cb, struct os_mbuf *om) {
    int rc;
    uint16_t auth_attr_handle;
    struct ble_gatt_chr uh;

    dgr_find_chr_by_uuid(&characteristics, &authentication_uuid.u, &uh);

    if(uh.val_handle != 0) {
        auth_attr_handle = uh.val_handle;

        rc = ble_gattc_write(conn_handle, auth_attr_handle, om, cb, NULL);
        if(rc != 0) {
            ESP_LOGE(tag_gatt, "Error while writing characteristic. handle = 0x%04x, rc = 0x%04x",
                auth_attr_handle, rc);
        }
    } else {
        ESP_LOGE(tag_gatt, "Could not find val_handle for authentication uuid.");
    }
}

void
dgr_write_control_char(uint16_t conn_handle, ble_gatt_attr_fn *cb, struct os_mbuf *om) {
    int rc;
    uint16_t cont_attr_handle;
    struct ble_gatt_chr uh;

    rc = dgr_find_chr_by_uuid(&characteristics, &control_uuid.u, &uh);

    if(rc == 0) {
        cont_attr_handle = uh.val_handle;

        rc = ble_gattc_write(conn_handle, cont_attr_handle, om, cb, NULL);
        if(rc != 0) {
            ESP_LOGE(tag_gatt, "Error while writing characteristic. handle = 0x%04x, rc = 0x%04x",
                cont_attr_handle, rc);
        }
    } else {
        ESP_LOGE(tag_gatt, "Could not find val_handle for control uuid.");
    }
}

void
dgr_send_bond_request_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);

    if(om) {
        dgr_build_bond_request_msg(om);
        ESP_LOGI(tag_gatt, "[06] BondRequest: sending message.");
        dgr_write_auth_char(conn_handle, dgr_send_bond_request_cb, om);
    }
}

void
dgr_send_keep_alive_msg(uint16_t conn_handle, uint8_t time) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);

    if(om) {
        dgr_build_keep_alive_msg(om, time);
        ESP_LOGI(tag_gatt, "[05] KeepAlive: sending message. time = %d", time);
        dgr_write_auth_char(conn_handle, dgr_send_keep_alive_cb, om);
    }
}

void
dgr_send_auth_request_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);

    if(om) {
        dgr_build_auth_request_msg(om);
        ESP_LOGI(tag_gatt, "[01] AuthRequest: sending message.");
        dgr_write_auth_char(conn_handle, dgr_send_auth_request_cb, om);
    }
}

void
dgr_send_auth_challenge_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);

    if(om) {
        dgr_build_auth_challenge_msg(om);
        ESP_LOGI(tag_gatt, "[03] AuthChallenge: sending message.");
        dgr_write_auth_char(conn_handle, dgr_send_auth_challenge_cb, om);
    }
}

void
dgr_send_glucose_tx_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);

    if(om) {
        dgr_build_glucose_tx_msg(om);
        ESP_LOGI(tag_gatt, "[08] GlucoseTx: sending message");
        dgr_write_control_char(conn_handle, dgr_send_glucose_tx_msg_cb, om);
    }
}


/*****************************************************************************
 * reading                                                                   *
 *****************************************************************************/
void
dgr_read_auth_char(uint16_t conn_handle, ble_gatt_attr_fn *cb) {
    int rc;
    struct ble_gatt_chr uh;

    rc = dgr_find_chr_by_uuid(&characteristics, &authentication_uuid.u, &uh);

    if(rc == 0) {
        rc = ble_gattc_read(conn_handle, uh.val_handle, cb, NULL);
        if(rc != 0) {
            ESP_LOGE(tag_gatt, "Error while reading characteristic. handle = %d, rc = %d",
                     uh.val_handle, rc);
        }
    } else {
        ESP_LOGE(tag_gatt, "Could not find val_handle for authentication uuid.");
    }
}

void
dgr_read_auth_challenge_msg(uint16_t conn_handle) {
    ESP_LOGI(tag_gatt, "[02] AuthChallenge: reading characteristic.");
    dgr_read_auth_char(conn_handle, dgr_read_auth_challenge_cb);
}

void
dgr_read_auth_status_msg(uint16_t conn_handle) {
    ESP_LOGI(tag_gatt, "[04] AuthStatus: reading characteristic.");
    dgr_read_auth_char(conn_handle, dgr_read_auth_status_cb);
}


/*****************************************************************************
 * callbacks                                                                 *
 *****************************************************************************/
void
dgr_print_cb_info(const struct ble_gatt_error *error, struct ble_gatt_attr *attr) {
    if(error != NULL) {
        ESP_LOGI(tag_gatt, "\tstatus      = 0x%x", error->status);
        ESP_LOGI(tag_gatt, "\tatt_handle  = 0x%x", error->att_handle);
    }
    if(attr != NULL) {
        ESP_LOGI(tag_gatt, "\tattr_handle = 0x%x", attr->handle);
        ESP_LOGI(tag_gatt, "\toffset      = %d", attr->offset);
    }
}

int
dgr_discover_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_svc *service, void *arg) {
    if(error->status == BLE_HS_EDONE) {
        ESP_LOGI(tag_gatt, "Service discovery : finished.");
        dgr_discover_descriptors(conn_handle);
    } else {
        ESP_LOGI(tag_gatt, "Service discovery : status=%d, att_handle=%d",
                 error->status, error->att_handle);

        if (service != NULL) {
            list_elm *le = dgr_create_svc_list_elm(*service);
            //dgr_print_list_elm(le);

            dgr_add_to_list(&services, le);
            return 0;
        } else {
            ESP_LOGE(tag_gatt, "Service discovery : Service is NULL");
        }
    }

    return 0;
}

int
dgr_discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_chr *chr, void *arg) {
    if(error->status == BLE_HS_EDONE) {
        ESP_LOGI(tag_gatt, "Characteristics discovery: finished.");

        dgr_print_list(&services);
        dgr_print_list(&characteristics);
        dgr_print_list(&descriptors);

        dgr_send_auth_request_msg(conn_handle);
    } else {
        ESP_LOGI(tag_gatt, "Characteristics discovery: status = %d, att_handle = %d",
                 error->status, error->att_handle);
        if (chr != NULL) {
            list_elm *le = dgr_create_chr_list_elm(*chr);
            //dgr_print_list_elm(le);

            dgr_add_to_list(&characteristics, le);
        } else {
            ESP_LOGE(tag_gatt, "Characteristics discovery: characteristic is NULL");
        }
    }

    return 0;
}

int
dgr_discover_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    if(error->status == BLE_HS_EDONE) {
        ESP_LOGI(tag_gatt, "Descriptor discovery: finished.");
        dgr_discover_characteristics(conn_handle);
    } else {
        ESP_LOGI(tag_gatt, "Descriptor discovery: status = %d, att_handle = %d",
                error->status, error->att_handle);

        if(dsc != NULL) {
            list_elm *le = dgr_create_dsc_list_elm(*dsc);
            //dgr_print_list_elm(le);

            dgr_add_to_list(&descriptors, le);
        }
    }

    return 0;
}

int
dgr_write_attr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "Write callback.");

    dgr_print_cb_info(error, attr);
    return 0;
}

int
dgr_send_auth_request_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[01] AuthRequest: write callback.");

    dgr_print_cb_info(error, attr);
    dgr_read_auth_challenge_msg(conn_handle);
    return 0;
}

int
dgr_read_auth_challenge_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[02] AuthChallenge: read callback.");

    dgr_print_cb_info(error, attr);
    if(attr && attr->om) {
        bool correct_token = true;

        dgr_print_rx_packet(attr->om);
        dgr_parse_auth_challenge_msg(attr->om->om_data, attr->om->om_len, &correct_token);

        if(correct_token) {
            dgr_send_auth_challenge_msg(conn_handle);
        } else {
            ESP_LOGE(tag_gatt, "Received encrypted token does not have the expected value.");
            dgr_print_token_details();
        }
    } else {
        ESP_LOGE(tag_gatt, "[02] AuthChallenge: mbuf not initialized");
    }
    return 0;
}

int
dgr_send_auth_challenge_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[03] AuthChallenge: write callback.");

    dgr_print_cb_info(error, attr);
    dgr_read_auth_status_msg(conn_handle);
    return 0;
}

int
dgr_read_auth_status_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[04] AuthStatus: read callback.");

    dgr_print_cb_info(error, attr);
    if(attr && attr->om) {
        dgr_print_rx_packet(attr->om);
        dgr_parse_auth_status_msg(attr->om->om_data, attr->om->om_len);
        dgr_send_keep_alive_msg(conn_handle, 25);
    } else {
        ESP_LOGE(tag_gatt, "[04] AuthStatus: read callback: mbuf not initialized");
    }
    return 0;
}

int
dgr_send_keep_alive_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[05] KeepAlive: write callback.");

    dgr_print_cb_info(error, attr);
    dgr_send_bond_request_msg(conn_handle);
    return 0;
}

int
dgr_send_bond_request_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[06] BondRequest: write callback.");

    dgr_print_cb_info(error, attr);
    return 0;
}

int
dgr_send_glucose_tx_msg_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[08] GlucoseTx: write callback");

    dgr_print_cb_info(error, attr);
    return 0;
}

int
dgr_send_notification_enable_msg_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "[07] Enabling notifications: write callback.");

    dgr_print_cb_info(error, attr);
    dgr_send_glucose_tx_msg(conn_handle);
    return 0;
}
