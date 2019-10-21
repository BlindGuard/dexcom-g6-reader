#include <host/ble_hs.h>
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

list characteristics = {.head = NULL, .length = 0};

// ble_gatt_disc_svc_fn
int dgr_discover_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *service, void *arg);
// ble_gatt_chr_fn
int dgr_discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr, void *arg);
// ble_gatt_attr_fn
int dgr_write_attr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg);

void
dgr_send_notification_enable_msg(uint16_t conn_handle) {
    // enable notifications by writing two bytes (1, 0) to
    // the CCCD (==control_uuid ?)
    // TODO: find right handle?
    uint8_t data[2] = {1, 0};
    struct ble_gatt_chr uh;
    uint16_t handle = 0;
    int rc;

    dgr_find_in_list(&characteristics, &control_uuid.u, &uh);
    if(uh.val_handle != 0) {
        handle = uh.val_handle;

        ESP_LOGI(tag_gatt, "Enabling notifications.");
        rc = ble_gattc_write_flat(conn_handle, handle, data, sizeof data, dgr_write_attr_cb, NULL);
        if (rc != 0) {
            ESP_LOGE(tag_gatt, "Error while enabling notifications. handle = %d, rc = %d",
                     handle, rc);
        }
    } else {
        ESP_LOGE(tag_gatt, "Could not find val_handle for control uuid.");
    }
}

void
dgr_send_bond_request_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);
    int rc;
    uint16_t auth_attr_handle;

    if(om) {
        dgr_build_bond_request_msg(om);
        struct ble_gatt_chr uh;
        dgr_find_in_list(&characteristics, &authentication_uuid.u, &uh);

        if(uh.val_handle != 0) {
            auth_attr_handle = uh.val_handle;

            ESP_LOGI(tag_gatt, "Sending bond request message.");
            rc = ble_gattc_write(conn_handle, auth_attr_handle, om, dgr_write_attr_cb, NULL);
            if(rc != 0) {
                ESP_LOGE(tag_gatt, "Error while writing characteristic. handle = %d, rc = %d",
                         auth_attr_handle, rc);
            }
        } else {
            ESP_LOGE(tag_gatt, "Could not find val_handle for authentication uuid.");
        }
    }
}

void
dgr_send_keep_alive_msg(uint16_t conn_handle, uint8_t time) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);
    int rc;
    uint16_t auth_attr_handle;

    if(om) {
        dgr_build_keep_alive_msg(om, time);
        struct ble_gatt_chr uh;
        dgr_find_in_list(&characteristics, &authentication_uuid.u, &uh);

        if(uh.val_handle != 0) {
            auth_attr_handle = uh.val_handle;

            ESP_LOGI(tag_gatt, "Sending Keep Alive message. time = %d", time);
            rc = ble_gattc_write(conn_handle, auth_attr_handle, om, dgr_write_attr_cb, NULL);
            if(rc != 0) {
                ESP_LOGE(tag_gatt, "Error while writing characteristic. handle = %d, rc = %d",
                         auth_attr_handle, rc);
            }
        } else {
            ESP_LOGE(tag_gatt, "Could not find val_handle for authentication uuid.");
        }
    }
}

void
dgr_send_auth_request_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);
    int rc = 0;
    uint16_t auth_attr_handle;

    if(om) {
        dgr_build_auth_request_msg(om);
        struct ble_gatt_chr uh;
        rc = dgr_find_in_list(&characteristics, &authentication_uuid.u, &uh);

        if(rc == 0) {
            auth_attr_handle = uh.val_handle;

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
    } else {
        ESP_LOGE(tag_gatt, "mbuf error");
    }
}

void
dgr_send_auth_challenge_msg(uint16_t conn_handle) {
    struct os_mbuf *om = os_mbuf_get_pkthdr(&dgr_mbuf_pool, 0);
    int rc;
    uint16_t auth_attr_handle = 0;

    if(om) {
        dgr_build_auth_challenge_msg(om);
        struct ble_gatt_chr uh;
        dgr_find_in_list(&characteristics, &authentication_uuid.u, &uh);

        if(uh.val_handle != 0) {
            auth_attr_handle = uh.val_handle;

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
dgr_discover_services(uint16_t conn_handle) {
    int rc;

    rc = ble_gattc_disc_all_chrs(conn_handle, 1, 65535, dgr_discover_chr_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(tag_gatt, "Error calling service discovery. rc = %d", rc);
    }
}

void
dgr_handle_rx(struct os_mbuf *om, uint16_t attr_handle, uint16_t conn_handle) {
    if(om && om->om_len > 0) {
        uint8_t op = om->om_data[0];

        dgr_print_rx_packet(om);

        switch(op) {
            case AUTH_CHALLENGE_RX_OPCODE: {
                bool correct_token = true;
                dgr_parse_auth_challenge_msg(om->om_data, om->om_len, &correct_token);

                if(correct_token) {
                    dgr_send_auth_challenge_msg(conn_handle);
                } else {
                    ESP_LOGE(tag_gatt, "Received encrypted token does not have the expected value.");
                    dgr_print_token_details();
                }
                break;
            }
            case AUTH_STATUS_RX_OPCODE: {
                dgr_parse_auth_status_msg(om->om_data, om->om_len);
                dgr_send_keep_alive_msg(conn_handle, 25);
                dgr_send_bond_request_msg(conn_handle);
                dgr_send_notification_enable_msg(conn_handle);
                break;
            }
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
    if(error->status == BLE_HS_EDONE) {
        ESP_LOGI(tag_gatt, "Service discovery finished.");
    } else {
        char buf[BLE_UUID_STR_LEN];
        ble_uuid_to_str(&service->uuid.u, buf);
        ESP_LOGI(tag_gatt, "Service discovery: status=%d, att_handle=%d, uuid = %s",
                 error->status, error->att_handle, buf);

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
    if(error->status == BLE_HS_EDONE) {
        ESP_LOGI(tag_gatt, "Characteristics discovery finished.");

        dgr_print_list(&characteristics);
        dgr_send_auth_request_msg(conn_handle);
    } else {
        //char buf[BLE_UUID_STR_LEN];

        ESP_LOGI(tag_gatt, "Characteristics discovery: status=%d, att_handle=%d",
                 error->status, error->att_handle);
        if (chr != NULL) {
            /*
            ESP_LOGI(tag_gatt, "Characteristic:");
            ESP_LOGI(tag_gatt, "\tdef_handle = %d", chr->def_handle);
            ESP_LOGI(tag_gatt, "\tval_handle = %d", chr->val_handle);
            ESP_LOGI(tag_gatt, "\tproperties = %d", chr->properties);
            ble_uuid_to_str(&chr->uuid.u, buf);
            ESP_LOGI(tag_gatt, "\tuuid       = %s", buf);
             */

            dgr_add_to_list(&characteristics, chr);
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