#include <host/ble_gap.h>
#include "nvs_flash.h"

// BLE
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "dexcom_g6_reader.h"

static const char *tag = "[Dexcom-G6-Reader][main]";
// 8 digit sensor id
const char *sensor_id = "812345";

int dgr_gap_event(struct ble_gap_event *event, void *arg);

bool
dgr_check_conn_candidate(struct ble_hs_adv_fields *adv_fields) {
    int i;

    // check if name is equal to desired sensor id
    if(adv_fields->name != NULL && adv_fields->name_len == 6) {
        for (i = 0; i < 6; i++) {
            if (adv_fields->name[i] != sensor_id[i]) {
                ESP_LOGE(tag, "Connection candidate has the wrong name.");
                return false;
            }
        }
        ESP_LOGI(tag, "Found a connection candidate.");
        return true;
    }

    ESP_LOGE(tag, "Connection candidate name not set or wrong length.");
    return false;
}

void
dgr_connect(const struct ble_gap_disc_desc *disc) {
    int rc;

    // scanning must be stopped before a connection
    rc = ble_gap_disc_cancel();
    if(rc != 0) {
        ESP_LOGE(tag, "Failed to cancel scan. rc = %d", rc);
        return;
    }

    // connection attempt
    rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &disc->addr, 30000, NULL,
            dgr_gap_event, NULL);
    if(rc != 0) {
        ESP_LOGE(tag, "Connection attempt failed: addr_type: %d, addr: %s",
                disc->addr.type, addr_to_string(disc->addr.val));
    }
}

void
dgr_evaluate_adv_report(const struct ble_gap_disc_desc *disc) {
    int rc;
    struct ble_hs_adv_fields adv_fields;

    rc = ble_hs_adv_parse_fields(&adv_fields, disc->data, disc->length_data);
    if(rc != 0) {
        return;
    }

    // log advertisement
    print_adv_fields(&adv_fields);

    // connect if connection candidate is desired device
    if(dgr_check_conn_candidate(&adv_fields)) {
        dgr_connect(disc);
    }
}

void
dgr_start_scan(void) {
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    rc = ble_hs_id_infer_auto(1, &own_addr_type);
    if(rc != 0) {
        ESP_LOGE(tag, "Error while determining address type. rc = %d", rc);
        return;
    }

    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;

    // default values
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      dgr_gap_event, NULL);
    if(rc != 0) {
        ESP_LOGE(tag, "Error in GAP discovery procedure. rc = %d", rc);
    }
}

int
dgr_gap_event(struct ble_gap_event *event, void *arg) {
	switch(event->type) {
	    case BLE_GAP_EVENT_CONNECT:
	        // new connection established or connection attempt failed
	        if(event->connect.status != 0) {
	            // connection attempt failed
	            ESP_LOGE(tag, "Connection attempt failed. error code: %d",
	                    event->connect.status);

                // rerun scan
                dgr_start_scan();
	        } else {
	            // connection successfully
	            ESP_LOGI(tag, "Connection successfull. handle: %d",
	                    event->connect.conn_handle);

                // start discovery of service
                dgr_discover_services(event->connect.conn_handle);
	            return 0;
	        }

		case BLE_GAP_EVENT_DISC:
			// event when an advertising report is received during
			// discovery procedure
			ESP_LOGI(tag, "GAP advertising report received");

			dgr_evaluate_adv_report(&event->disc);

			return 0;
		case BLE_GAP_EVENT_DISC_COMPLETE:
			// discovery completes when timed out or when
			// a connection is initiated (?)
			ESP_LOGI(tag, "GAP discovery procedure completed, reason = %d",
					event->disc_complete.reason);

			if(event->disc_complete.reason == 0) {
			    // rerun scan for timed out scan
			    dgr_start_scan();
			}

			return 0;
	    case BLE_GAP_EVENT_NOTIFY_RX:
            ESP_LOGI(tag, "Received message, type=%s",
                event->notify_rx.indication == 0 ? "Notification" : "Indication");
            dgr_handle_rx(event->notify_rx.om, event->notify_rx.attr_handle,
                event->notify_rx.conn_handle);

            return 0;
		default:
			ESP_LOGI(tag, "Not processed event with type: %d", event->type);
			return 0;
	}
}

void
dgr_sync_callback(void) {
	int rc;

	// check for proper identity address
	rc = ble_hs_util_ensure_addr(0);
	assert(rc == 0);

	// start device scan
	ESP_LOGI(tag, "Host and Controller synced. Starting device scan.");
	dgr_start_scan();
}

void
dgr_reset_callback(int reason) {
    ESP_LOGI(tag, "Resetting state, reason:%d", reason);

    // restart scan (?)
}

void
dgr_host_task(void *param) {
	ESP_LOGI(tag, "BLE Host Task started.");
	nimble_port_run();
	nimble_port_freertos_deinit();
}

void
app_main(void) {
	//int rc;

	// initialize NVS flash
	esp_err_t ret = nvs_flash_init();
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// initialize ESP controller and transport layer
	ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());

	// initialize host stack
	nimble_port_init();

	// initialize mbuf pool
    dgr_create_mbuf_pool();
    // initialize aes context
    dgr_create_crypto_context();

	// initialize NimBLE host configuration and callbacks
	// sync callback (controller and host sync, executed at startup/reset)
	ble_hs_cfg.sync_cb = dgr_sync_callback;
	// reset callback (executed after fatal error)
	ble_hs_cfg.reset_cb = dgr_reset_callback;
	// store status callback (executed when persistence operation cannot be performed)
	//ble_hs_cfg.store_status_cb =

	ble_svc_gap_init();
	ble_svc_gatt_init();
	
	// run host stack thread
	nimble_port_freertos_init(dgr_host_task);
}
