#include "dexcom_g6_reader.h"

const char* tag_gatt = "[Dexcom-G6-Reader][gatt]";
// Transmitter services
// FEBC
const ble_uuid16_t advertisement_uuid = BLE_UUID16_INIT(0xfebc);
// F8083532-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t cgm_service_uuid =
    BLE_UUID128_INIT(0xf8, 0x08, 0x35, 0x32, 0x84, 0x9e, 0x53, 0x1c,
                     0xc5, 0x94, 0x30, 0xf1, 0xf8, 0x6a, 0x4e, 0xa5);

// CGM Service characteristics
// F8083534-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t control_uuid =
    BLE_UUID128_INIT(0xf8, 0x08, 0x35, 0x34, 0x84, 0x9e, 0x53, 0x1c,
                     0xc5, 0x94, 0x30, 0xf1, 0xf8, 0x6a, 0x4e, 0xa5);
// F8083535-849E-531C-C594-30F1F86A4EA5
const ble_uuid128_t authentication_uuid =
    BLE_UUID128_INIT(0xf8, 0x08, 0x35, 0x35, 0x84, 0x9e, 0x53, 0x1c,
                     0xc5, 0x94, 0x30, 0xf1, 0xf8, 0x6a, 0x4e, 0xa5);

typedef struct uuid_handles {
    ble_uuid128_t uuid;
    uint16_t val_handle;
    uint16_t def_handle;
} uuid_handles;

struct uuid_handles characteristics[4];
int char_count = 0;

// ble_gatt_disc_svc_fn
int dgr_discover_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *service, void *arg);
// ble_gatt_chr_fn
int dgr_discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr, void *arg);
// ble_gatt_attr_fn
int dgr_write_attr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg);

uint16_t
dgr_get_char_val_handle(const ble_uuid_t* uuid) {
    for(int i = 0; i < 4; i++) {
        if(ble_uuid_cmp(uuid, &characteristics[i].uuid.u) == 0) {
            return characteristics[i].val_handle;
        }
    }

    return 0;
}

void
dgr_send_auth_request_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);
    int rc = 0;
    uint16_t auth_attr_handle;

    if(om) {
        dgr_build_auth_request_msg(om);
        auth_attr_handle = dgr_get_char_val_handle(&authentication_uuid.u);

        if(auth_attr_handle != 0) {
            ESP_LOGI(tag_gatt, "[01] Sending AuthRequest message. handle = %d",
                auth_attr_handle);
            rc = ble_gattc_write(conn_handle, auth_attr_handle, om, dgr_write_attr_cb, NULL);
            if (rc != 0) {
                ESP_LOGE(tag_gatt, "Error while writing characteristic. handle = %d, rc = %d",
                         auth_attr_handle, rc);
            }
        } else {
            ESP_LOGE(tag_gatt, "Could not find val_handle for authentication uuid.");
        }
    }
}

void
dgr_send_auth_challenge_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);
    int rc;
    uint16_t auth_attr_handle;

    if(om) {
        dgr_build_auth_challenge_msg(om);
        auth_attr_handle = dgr_get_char_val_handle(&authentication_uuid.u);

        if(auth_attr_handle != 0) {
            ESP_LOGI(tag_gatt, "[03] Sending AuthChallenge message. handle = %d",
                auth_attr_handle);
            rc = ble_gattc_write(conn_handle, auth_attr_handle, om, dgr_write_attr_cb, NULL);
            if (rc != 0) {
                ESP_LOGE(tag_gatt, "Error while writing characteristic. handle = %d, rc = %d",
                         auth_attr_handle, rc);
            }
        } else {
            ESP_LOGE(tag_gatt, "Could not find val_handle for authentication uuid.");
        }
    }
}

void
dgr_discover_service(uint16_t conn_handle, const ble_uuid_t *svc_uuid) {
    int rc;
    char buf[BLE_UUID_STR_LEN];

    ESP_LOGI(tag_gatt, "Performing service discovery for: connection_handle = %d and uuid =  %s",
            conn_handle, ble_uuid_to_str(svc_uuid, buf));

    rc = ble_gattc_disc_svc_by_uuid(conn_handle, svc_uuid,
            dgr_discover_service_cb, NULL);
    if(rc != 0) {
        ESP_LOGE(tag_gatt, "Error calling service discovery. rc = %d", rc);
    }
}

void
dgr_handle_rx(struct os_mbuf *om, uint16_t attr_handle, uint16_t conn_handle) {
    if(om && om->om_len > 0) {
        uint8_t op = om->om_data[0];

        switch(op) {
            case AUTH_CHALLENGE_RX_OPCODE:
                dgr_parse_auth_challenge_msg(om->om_data, om->om_len);
                dgr_send_auth_challenge_msg(conn_handle);
                break;
            case AUTH_STATUS_RX_OPCODE:
                dgr_parse_auth_status_msg(om->om_data, om->om_len);
                break;
            default:
                break;
        }
    }
}


/*****************************************************************************
 * callbacks                                                                 *
 *****************************************************************************/

int
dgr_discover_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_svc *service, void *arg) {
    if(error->status == 14) {
        ESP_LOGI(tag_gatt, "Service discovery finished.");

        // TODO: something? start authentication?
        dgr_send_auth_request_msg(conn_handle);
    } else {
        ESP_LOGI(tag_gatt, "Service discovery: status=%d, att_handle=%d",
                 error->status, error->att_handle);

        if (service != NULL) {
            int rc = ble_gattc_disc_all_chrs(conn_handle, service->start_handle,
                                         service->end_handle, dgr_discover_chr_cb, NULL);
            if (rc != 0) {
                ESP_LOGE(tag_gatt, "Error calling characteristics discovery. rc = %d", rc);
            }

            return rc;
        } else {
            ESP_LOGE(tag_gatt, "Discover Service callback: Service is NULL");
        }
    }

    return 0;
}

int
dgr_discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_chr *chr, void *arg) {
    if(error->status == 14) {
        ESP_LOGI(tag_gatt, "Characteristics discovery finished. Table:");

        for(int i = 0; i < 4; i++) {
            char buf[BLE_UUID_STR_LEN];
            ble_uuid_to_str(&characteristics[i].uuid.u, buf);

            ESP_LOGI(tag_gatt, "Entry %d", i);
            ESP_LOGI(tag_gatt, "\tuuid       = %s", buf);
            ESP_LOGI(tag_gatt, "\tval_handle = %d", characteristics[i].val_handle);
            ESP_LOGI(tag_gatt, "\tdef_handle = %d", characteristics[i].def_handle);
        }
    } else {
        char buf[BLE_UUID_STR_LEN];

        ESP_LOGI(tag_gatt, "Characteristics discovery: status=%d, att_handle=%d",
                 error->status, error->att_handle);
        if (chr != NULL) {
            ESP_LOGI(tag_gatt, "Characteristic:");
            ESP_LOGI(tag_gatt, "\tdef_handle = %d", chr->def_handle);
            ESP_LOGI(tag_gatt, "\tval_handle = %d", chr->val_handle);
            ESP_LOGI(tag_gatt, "\tproperties = %d", chr->properties);
            ble_uuid_to_str(&chr->uuid.u, buf);
            ESP_LOGI(tag_gatt, "\tuuid       = %s", buf);

            //TODO: save characteristics somewhere?
            if(chr->uuid.u.type == BLE_UUID_TYPE_128) {
                // save only 128bit uuids for now
                uuid_handles n = {.uuid = chr->uuid.u128,
                                  .def_handle = chr->def_handle,
                                  .val_handle = chr->val_handle};
                characteristics[char_count] = n;
            }
        } else {
            ESP_LOGE(tag_gatt, "Discover Characteristic callback: Characteristic is NULL");
        }
    }

    return 0;
}

void
dgr_print_cb_info(const struct ble_gatt_error *error, struct ble_gatt_attr *attr) {
    if(error != NULL) {
        ESP_LOGI(tag_gatt, "\tstatus = 0x%x", error->status);
        ESP_LOGI(tag_gatt, "\tatt_handle = %d", error->att_handle);
    }
    if(attr != NULL) {
        ESP_LOGI(tag_gatt, "\tattr_handle = %d", attr->handle);
        ESP_LOGI(tag_gatt, "\toffset      = %d", attr->offset);
    }
}

int
dgr_write_attr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        struct ble_gatt_attr *attr, void *arg) {
    ESP_LOGI(tag_gatt, "Write callback.");

    dgr_print_cb_info(error, attr);
    return 0;
}