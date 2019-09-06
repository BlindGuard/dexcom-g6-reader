#include <host/ble_gatt.h>
#include <esp_log.h>

#include "messages.c"

const char* tag_gatt = "[Dexcom-G6-Reader][gatt]";
// ble_gatt_disc_svc_fn
int dgr_discover_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_svc *service, void *arg);
// ble_gatt_chr_fn
int dgr_discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_chr *chr, void *arg);

void
dgr_discover_service(uint16_t conn_handle, const ble_uuid_t *svc_uuid) {
    int rc;
    char buf[BLE_UUID_STR_LEN];

    ESP_LOGI(tag_gatt, "Performing service discovery for connection: %d and uuid: %s",
            conn_handle, ble_uuid_to_str(svc_uuid, buf));

    rc = ble_gattc_disc_svc_by_uuid(conn_handle, svc_uuid,
            dgr_discover_service_cb, NULL);
    if(rc != 0) {
        ESP_LOGE(tag_gatt, "Error calling service discovery. rc = %d", rc);
    }
}

int
dgr_discover_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_svc *service, void *arg) {
    int rc;
    ESP_LOGI(tag_gatt, "Service discovery: status=%d, att_handle=%d",
            error->status, error->att_handle);

    rc = ble_gattc_disc_all_chrs(conn_handle, service->start_handle,
            service->end_handle, dgr_discover_chr_cb, NULL);
    if(rc != 0) {
        ESP_LOGE(tag_gatt, "Error calling characteristics discovery. rc = %d", rc);
    }

    return rc;
}

int
dgr_discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
        const struct ble_gatt_chr *chr, void *arg) {
    //TODO: save characteristics somewhere?
    ESP_LOGI(tag_gatt, "Characteristics discovery: status=%d, att_handle=%d",
            error->status, error->att_handle);
    return 0;
}