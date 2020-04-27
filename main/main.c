#include "nvs_flash.h"
#include <esp_sleep.h>
#include "esp_log.h"

// BLE
#include <host/ble_gap.h>
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "dexcom_g6_reader.h"


RTC_DATA_ATTR int boot_count = 0;
RTC_DATA_ATTR int error_count = 0;
static const char *tag = "[Dexcom-G6-Reader][main]";
const char *transmitter_id = "812345";

int dgr_gap_event(struct ble_gap_event *event, void *arg);

void
dgr_error() {
    //TODO: error count, stuff etc
    error_count++;
    ESP_LOGE(tag, "Error count = %d", error_count);

    ESP_LOGE(tag, "Going to deep sleep after error for %d seconds", SLEEP_AFTER_ERROR);
    esp_deep_sleep(SLEEP_AFTER_ERROR * 1000000); // time is in microseconds
}

bool
dgr_check_conn_candidate(struct ble_hs_adv_fields *adv_fields) {
    // sensor name is DexcomXX, where XX are the last 2 digits of
    // the transmitter id
    //TODO: add advertisement uuid to check
    if(adv_fields->name != NULL) {
        int name_len = adv_fields->name_len;
        if(adv_fields->name[name_len - 1] == transmitter_id[5] &&
           adv_fields->name[name_len - 2] == transmitter_id[4]) {

            ESP_LOGI(tag, "Found a connection candidate.");
            return true;
        }
    }

    ESP_LOGE(tag, "Connection candidate name not set or wrong.");
    return false;
}

bool
dgr_check_bond_state(uint16_t conn_handle) {
    struct ble_gap_conn_desc conn_desc;
    ble_gap_conn_find(conn_handle, &conn_desc);

    return (conn_desc.sec_state.bonded == 1U);
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
    //print_adv_fields(&adv_fields);

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
                return 0;
	        } else {
	            // connection successfully
	            ESP_LOGI(tag, "Connection successfull. handle = %d",
	                    event->connect.conn_handle);
	            // TODO: remove or make debug output?
                struct ble_gap_conn_desc conn_desc;
                ble_gap_conn_find(event->enc_change.conn_handle, &conn_desc);
                dgr_print_conn_sec_state(conn_desc.sec_state);


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
            ESP_LOGI(tag, "Received message, type = %s",
                event->notify_rx.indication == 0 ? "Notification" : "Indication");
            dgr_handle_rx(event->notify_rx.om, event->notify_rx.attr_handle,
                event->notify_rx.conn_handle);

            return 0;

	    case BLE_GAP_EVENT_DISCONNECT:
	        ESP_LOGI(tag, "Disconnect: handle = %d, reason = 0x%04x",
                       event->disconnect.conn.conn_handle, event->disconnect.reason);


            ESP_LOGI(tag, "Going to deep sleep for %d seconds", SLEEP_BETWEEN_READINGS);
            esp_deep_sleep(SLEEP_BETWEEN_READINGS * 1000000); // time is in microseconds


	    case BLE_GAP_EVENT_ENC_CHANGE:
	        ESP_LOGI(tag, "Encryption changed: handle = %d, status = 0x%04x",
	            event->enc_change.conn_handle, event->enc_change.status);
	        struct ble_gap_conn_desc conn_desc;
	        ble_gap_conn_find(event->enc_change.conn_handle, &conn_desc);
            dgr_print_conn_sec_state(conn_desc.sec_state);

            dgr_send_notification_enable_msg(event->enc_change.conn_handle,
                                             &control_uuid.u, dgr_send_control_enable_notif_cb, 1);
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
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    boot_count++;

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
    if(wakeup_cause != ESP_SLEEP_WAKEUP_TIMER) {
        // create ringbuffer
        // ringbuffer is in RTC memory so we dont need to initialize it when waking up
        dgr_init_ringbuffer();
    }

	// initialize NimBLE host configuration and callbacks
	// sync callback (controller and host sync, executed at startup/reset)
	ble_hs_cfg.sync_cb = dgr_sync_callback;
	// reset callback (executed after fatal error)
	ble_hs_cfg.reset_cb = dgr_reset_callback;

	ble_svc_gap_init();
	ble_svc_gatt_init();
	
	// run host stack thread
	nimble_port_freertos_init(dgr_host_task);
}
