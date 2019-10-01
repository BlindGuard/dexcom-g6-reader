#include <stdio.h>

#include "modlog/modlog.h"
#include "nimble/ble.h"
#include "host/ble_hs_adv.h"

#include "dexcom_g6_reader.h"

char*
addr_to_string(const void *addr) {
    static char buf[18];
    const uint8_t *val = addr;

    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
            val[5], val[4], val[3], val[2], val[1], val[0]);

    return buf;
}

void
print_bytes_debug(uint8_t *bytes, int len) {
    int i;

    for(i = 0; i < len; i++) {
        MODLOG_DFLT(DEBUG, "%s%02x", i != 0 ? ":" : "", bytes[i]);
    }
}

void
print_adv_fields(struct ble_hs_adv_fields *adv_fields) {
    register int i;
    char buf[32];
    char uuid_buf[BLE_UUID_STR_LEN];
    uint8_t *u8p;

    MODLOG_DFLT(INFO, "[=========== Received advertisement ============]\n");
    if(adv_fields->name != NULL) {
        MODLOG_DFLT(INFO, "Device name                (%scomplete) : \n",
                adv_fields->name_is_complete ? "" : "in");

        assert(adv_fields->name_len < sizeof buf - 1);
        memcpy(buf, adv_fields->name, adv_fields->name_len);
        buf[adv_fields->name_len] = '\0';
        MODLOG_DFLT(INFO, "\t%s\n", buf);
    }

    if(adv_fields->tx_pwr_lvl_is_present) {
        MODLOG_DFLT(INFO, "Tx power level                          : %d\n",
                adv_fields->tx_pwr_lvl);
    }

    if(adv_fields->flags != 0) {
        MODLOG_DFLT(INFO, "Flags                                   : 0x%02x\n",
                adv_fields->flags);
    }

    if(adv_fields->public_tgt_addr != NULL) {
        MODLOG_DFLT(INFO, "Public target address                   : \n");

        u8p = adv_fields->public_tgt_addr;
        for(i = 0; i < adv_fields->num_public_tgt_addrs; i++) {
            MODLOG_DFLT(INFO, "\t%s\n", addr_to_string(u8p));
            u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        }
    }

    if(adv_fields->uuids16 != NULL) {
        MODLOG_DFLT(INFO, "16-bit service class UUIDs (%scomplete) : \n",
                adv_fields->uuids16_is_complete ? "" : "in");

        for(i = 0; i < adv_fields->num_uuids16; i++) {
            ble_uuid_to_str(&adv_fields->uuids16[i].u, uuid_buf);
            MODLOG_DFLT(INFO, "\t%s\n", uuid_buf);
        }
    }

    if(adv_fields->uuids32 != NULL) {
        MODLOG_DFLT(INFO, "32-bit service class UUIDs (%scomplete) : \n",
                adv_fields->uuids32_is_complete ? "" : "in");

        for(i = 0; i < adv_fields->num_uuids32; i++) {
            ble_uuid_to_str(&adv_fields->uuids32[i].u, uuid_buf);
            MODLOG_DFLT(INFO, "\t%s\n", uuid_buf);
        }
    }

    if(adv_fields->uuids128 != NULL) {
        MODLOG_DFLT(INFO, "128-bit service class UUIDs (%scomplete) : \n",
                adv_fields->uuids128_is_complete ? "" : "in");

        for(i = 0; i < adv_fields->num_uuids128; i++) {
            ble_uuid_to_str(&adv_fields->uuids128[i].u, uuid_buf);
            MODLOG_DFLT(INFO, "\t%s\n", uuid_buf);
        }
    }

    if(adv_fields->svc_data_uuid16 != NULL) {
        MODLOG_DFLT(INFO, "16-bit service data                     : \n\t");

        print_bytes_debug(adv_fields->svc_data_uuid16, adv_fields->svc_data_uuid16_len);
        MODLOG_DFLT(INFO, "\n");
    }

    if(adv_fields->svc_data_uuid32 != NULL) {
        MODLOG_DFLT(INFO, "32-bit service data                     : \n\t");

        print_bytes_debug(adv_fields->svc_data_uuid32, adv_fields->svc_data_uuid32_len);
        MODLOG_DFLT(INFO, "\n");
    }

    if(adv_fields->svc_data_uuid128 != NULL) {
        MODLOG_DFLT(INFO, "128-bit service data                    : \n\t");

        print_bytes_debug(adv_fields->svc_data_uuid128, adv_fields->svc_data_uuid128_len);
        MODLOG_DFLT(INFO, "\n");
    }
}