#include <stdio.h>

#include "nimble/ble.h"
#include <host/ble_gap.h>

#include "dexcom_g6_reader.h"

const char* tag_util = "[Dexcom-G6-Reader][util]";

char*
addr_to_string(const void *addr) {
    static char buf[18];
    const uint8_t *val = addr;

    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
            val[5], val[4], val[3], val[2], val[1], val[0]);

    return buf;
}

void
print_adv_fields(struct ble_hs_adv_fields *adv_fields) {
    register int i;
    char buf[32];
    char uuid_buf[BLE_UUID_STR_LEN];
    uint8_t *u8p;

    ESP_LOGI(tag_util, "[=========== Received advertisement ============]");
    if(adv_fields->name != NULL) {
        ESP_LOGI(tag_util, "Device name                (%scomplete)  :",
                adv_fields->name_is_complete ? "" : "in");

        assert(adv_fields->name_len < sizeof buf - 1);
        memcpy(buf, adv_fields->name, adv_fields->name_len);
        buf[adv_fields->name_len] = '\0';
        ESP_LOGI(tag_util, "\t%s", buf);
    }

    if(adv_fields->tx_pwr_lvl_is_present) {
        ESP_LOGI(tag_util, "Tx power level                          : %d",
                adv_fields->tx_pwr_lvl);
    }

    if(adv_fields->flags != 0) {
        ESP_LOGI(tag_util, "Flags                                   : 0x%02x",
                adv_fields->flags);
    }

    if(adv_fields->public_tgt_addr != NULL) {
        ESP_LOGI(tag_util, "Public target address                   :");

        u8p = adv_fields->public_tgt_addr;
        for(i = 0; i < adv_fields->num_public_tgt_addrs; i++) {
            ESP_LOGI(tag_util, "\t%s", addr_to_string(u8p));
            u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        }
    }

    if(adv_fields->uuids16 != NULL) {
        ESP_LOGI(tag_util, "16-bit service class UUIDs (%scomplete) :",
                adv_fields->uuids16_is_complete ? "" : "in");

        for(i = 0; i < adv_fields->num_uuids16; i++) {
            ble_uuid_to_str(&adv_fields->uuids16[i].u, uuid_buf);
            ESP_LOGI(tag_util, "\t%s", uuid_buf);
        }
    }

    if(adv_fields->uuids32 != NULL) {
        ESP_LOGI(tag_util, "32-bit service class UUIDs (%scomplete) :",
                adv_fields->uuids32_is_complete ? "" : "in");

        for(i = 0; i < adv_fields->num_uuids32; i++) {
            ble_uuid_to_str(&adv_fields->uuids32[i].u, uuid_buf);
            ESP_LOGI(tag_util, "\t%s", uuid_buf);
        }
    }

    if(adv_fields->uuids128 != NULL) {
        ESP_LOGI(tag_util, "128-bit service class UUIDs (%scomplete) :",
                adv_fields->uuids128_is_complete ? "" : "in");

        for(i = 0; i < adv_fields->num_uuids128; i++) {
            ble_uuid_to_str(&adv_fields->uuids128[i].u, uuid_buf);
            ESP_LOGI(tag_util, "\t%s", uuid_buf);
        }
    }

    if(adv_fields->svc_data_uuid16 != NULL) {
        ESP_LOGI(tag_util, "16-bit service data                     :");

        ESP_LOG_BUFFER_HEX_LEVEL(tag_util, adv_fields->svc_data_uuid16,
            adv_fields->svc_data_uuid16_len, ESP_LOG_INFO);
    }

    if(adv_fields->svc_data_uuid32 != NULL) {
        ESP_LOGI(tag_util, "32-bit service data                     :");

        ESP_LOG_BUFFER_HEX_LEVEL(tag_util, adv_fields->svc_data_uuid32,
            adv_fields->svc_data_uuid32_len, ESP_LOG_INFO);
    }

    if(adv_fields->svc_data_uuid128 != NULL) {
        ESP_LOGI(tag_util, "128-bit service data                    :");

        ESP_LOG_BUFFER_HEX_LEVEL(tag_util, adv_fields->svc_data_uuid128,
            adv_fields->svc_data_uuid128_len, ESP_LOG_INFO);
    }
}

void dgr_print_rx_packet(struct os_mbuf *om) {
    ESP_LOGI(tag_util, "os_mbuf dump:");
    ESP_LOGI(tag_util, "\tpkthdr_len = %d", om->om_pkthdr_len);
    ESP_LOGI(tag_util, "\tdata_len   = %d", om->om_len);

    ESP_LOG_BUFFER_HEX_LEVEL(tag_util, om->om_data, om->om_len, ESP_LOG_INFO);
    /*
    for(int i = 0; i < om->om_len; i++) {
        ESP_LOGI(tag_util, "%02x ", om->om_data[i]);
        if(i != 0 && i % 10 == 0) {
            ESP_LOGI(tag_util, "\n\t");
        }
    }*/
}

void
dgr_print_conn_sec_state(struct ble_gap_sec_state conn_sec) {
    ESP_LOGI(tag_util, "[=========== connection security ===========]");
    ESP_LOGI(tag_util, "encrypted?      %s", conn_sec.encrypted == 1 ? "yes" : "no");
    ESP_LOGI(tag_util, "authenticated?  %s", conn_sec.authenticated == 1 ? "yes" : "no");
    ESP_LOGI(tag_util, "bonded?         %s", conn_sec.bonded == 1 ? "yes" : "no");
    ESP_LOGI(tag_util, "key size = %d", conn_sec.key_size);
}

uint32_t
make_u32_from_bytes_le(const uint8_t *bytes) {
    return bytes[0] | (uint32_t) bytes[1] << 8U
        | (uint32_t) bytes[2] << 16U | (uint32_t) bytes[3] << 24U;
}

uint16_t
make_u16_from_bytes_le(const uint8_t *bytes) {
    return bytes[0] | (uint16_t) bytes[1] << 8U;
}

void
write_u32_le(uint8_t *bytes, uint32_t in) {
    bytes[0] = in;
    bytes[1] = in >> 8U;
    bytes[2] = in >> 16U;
    bytes[3] = in >> 24U;
}

void
write_u16_le(uint8_t *bytes, uint32_t in) {
    bytes[0] = in;
    bytes[1] = in >> 8U;
}