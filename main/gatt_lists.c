#include "dexcom_g6_reader.h"

const char* tag_chrs = "[Dexcom-G6-Reader][lists]";

void
dgr_clear_list(list *l) {
    list_elm *le = l->head;

    while(true) {
        if(le->next != NULL) {
            list_elm *tmp = le;
            le = le->next;
            free(tmp);
        } else {
            free(le);
            break;
        }
    }

    l->head = NULL;
    l->tail = NULL;
    l->length = 0;
}

void
dgr_add_to_list(list *l, list_elm *le) {
    if(l->length == 0) {
        l->head = le;
        l->tail = le;
        l->length++;
    } else {
        l->tail->next = le;
        l->length++;
        l->tail = le;
    }
}

list_elm*
dgr_create_chr_list_elm(struct ble_gatt_chr chr) {
    list_elm *le = malloc(sizeof(list_elm));
    le->type = chr_lst_elm;
    le->chr = chr;
    le->next = NULL;

    return le;
}

list_elm*
dgr_create_dsc_list_elm(struct ble_gatt_dsc dsc) {
    list_elm *le = malloc(sizeof(list_elm));
    le->type = dsc_lst_elm;
    le->dsc = dsc;
    le->next = NULL;

    return le;
}

list_elm*
dgr_create_svc_list_elm(struct ble_gatt_svc svc) {
    list_elm *le = malloc(sizeof(list_elm));
    le->type = svc_lst_elm;
    le->svc = svc;
    le->next = NULL;

    return le;
}

int
dgr_find_chr_by_uuid(list *l, const ble_uuid_t *uuid, struct ble_gatt_chr *out) {
    list_elm le = *l->head;
    if(le.type != chr_lst_elm) {
        ESP_LOGE(tag_chrs, "Called dgr_find_chr_by_uuid with a list not containing characteristics.");
        return -1;
    }

    while(true) {
        if(ble_uuid_cmp(uuid, &(le.chr.uuid.u)) == 0) {
            *out = le.chr;
            return 0;
        } else if(le.next != NULL) {
            le = *le.next;
        } else {
            char buf[BLE_UUID_STR_LEN];
            ble_uuid_to_str(uuid, buf);
            ESP_LOGE(tag_chrs, "Could not find uuid = %s in characteristics list.", buf);
            return -1;
        }
    }
}

void
dgr_print_list_elm(list_elm *le) {
    char buf[BLE_UUID_STR_LEN];

    if(le->type == svc_lst_elm) {
        ble_uuid_to_str(&(le->svc.uuid.u), buf);
        ESP_LOGI(tag_chrs, "\tuuid = %s", buf);
        ESP_LOGI(tag_chrs, "\t\tstart_handle = 0x%04x", le->svc.start_handle);
        ESP_LOGI(tag_chrs, "\t\tend_handle   = 0x%04x", le->svc.end_handle);
    } else if(le->type == dsc_lst_elm) {
        ble_uuid_to_str(&(le->dsc.uuid.u), buf);
        ESP_LOGI(tag_chrs, "\tuuid = %s", buf);
        ESP_LOGI(tag_chrs, "\t\thandle       = 0x%04x", le->dsc.handle);
    } else {
        ble_uuid_to_str(&(le->chr.uuid.u), buf);
        ESP_LOGI(tag_chrs, "\tuuid = %s", buf);
        ESP_LOGI(tag_chrs, "\t\tval_handle   = 0x%04x", le->chr.val_handle);
        ESP_LOGI(tag_chrs, "\t\tdef_handle   = 0x%04x", le->chr.def_handle);
        ESP_LOGI(tag_chrs, "\t\tproperties   = 0x%02x", le->chr.properties);
    }
}

void
dgr_print_list(list *l) {
    list_elm *le = (l->head);

    if(le->type == svc_lst_elm) {
        ESP_LOGI(tag_chrs, "List of services        :");
    } else if(le->type == dsc_lst_elm) {
        ESP_LOGI(tag_chrs, "List of descriptors     :");
    } else {
        ESP_LOGI(tag_chrs, "List of characteristics :");
    }

    while(true) {
        dgr_print_list_elm(le);

        if(le->next == NULL) {
            break;
        } else {
            le = le->next;
        }
    }
}