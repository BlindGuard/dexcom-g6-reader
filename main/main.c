#include "esp_log.h"
#include "nvs_flash.h"

// BLE
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"

static const char *tag = "[Dexcom-G6-Reader]";

void ble_sync_callback(void) {
	// start device scan
	// https://mynewt.apache.org/latest/network/docs/ble_hs/ble_gap.html#c.ble_gap_disc
}

void ble_reset_callback(int reason) {
}

void ble_host_task(void *param) {
	ESP_LOGI(tag, "BLE Host Task started.");
	nimble_port_run();
	nimble_port_freertos_deinit();
}

void app_main(void) {
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

	// initialize NimBLE host configuration and callbacks
	// sync callback (controller and host sync, executed at startup/reset)
	ble_hs_cfg.sync_cb = ble_sync_callback;
	// reset callback (executed after fatal error)
	ble_hs_cfg.reset_cb = ble_reset_callback;
	// store status callback (executed when persistence operation cannot be performed)
	//ble_hs_cfg.store_status_cb = 
	
	// run host stack thread
	nimble_port_freertos_init(ble_host_task);
}
