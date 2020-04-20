#include <esp_sleep.h>
#include "dexcom_g6_reader.h"
#include "esp32/rom/crc.h"

#define BUFFER_SIZE         420     // 28 bytes * 15
#define BUFFER_TYPE         RINGBUF_TYPE_NOSPLIT
RTC_DATA_ATTR StaticRingbuffer_t buffer_struct;
RTC_DATA_ATTR uint8_t buffer_storage[BUFFER_SIZE];

static const char *tag_stg = "[Dexcom-G6-Reader][storage]";

void
dgr_init_ringbuffer() {
    rbuf_handle = xRingbufferCreateStatic(BUFFER_SIZE, BUFFER_TYPE, (uint8_t*)&buffer_storage, &buffer_struct);
}

void
dgr_save_to_ringbuffer(const uint8_t *in, uint8_t length) {
    size_t free_size = xRingbufferGetCurFreeSize(rbuf_handle);
    if(free_size >= 28) {
        UBaseType_t res = xRingbufferSend(rbuf_handle, in, length, pdMS_TO_TICKS(5000));

        if (res != pdTRUE) {
            ESP_LOGE(tag_stg, "Error while writing into ringbuffer. rc = %d", res);
        }
    } else {
        ESP_LOGE(tag_stg, "Ringbuffer is full. Printing debug info.");
        dgr_print_rbuf();
        //TODO: do something else
    }
}

//TODO: backfill
void
dgr_check_for_backfill_and_sleep(uint16_t conn_handle) {
    // enable backfill notifications
    dgr_send_notification_enable_msg(conn_handle, &backfill_uuid.u, dgr_send_backfill_enable_notif_cb, 0);
    //TODO: control flow -> waiting for backfill messages

    // tear down bt?
    //ESP_LOGI(tag_stg, "Going to deep sleep for %d seconds", SLEEP_BETWEEN_READINGS);
    //esp_deep_sleep(SLEEP_BETWEEN_READINGS * 1000000); // time is in microseconds
}

void
dgr_print_rbuf() {
    size_t item_size;
    uint8_t *data = (uint8_t *)xRingbufferReceive(rbuf_handle, &item_size, pdMS_TO_TICKS(1000));

    while(data != NULL) {
        uint8_t status = data[1];
        uint32_t sequence = make_u32_from_bytes_le(&data[2]);
        uint32_t timestamp = make_u32_from_bytes_le(&data[6]);
        uint16_t glucose = make_u16_from_bytes_le(&data[10]) & 0xfffU;
        uint8_t state = data[12];
        uint8_t trend = data[13];
        uint16_t crc = make_u16_from_bytes_le(&data[item_size - 2]);
        uint16_t crc_calc = ~crc16_be((uint16_t)~0x0000, data, item_size - 2);

        ESP_LOGI(tag_stg, "[=========== RingbufItem (size=%d) ===========]", item_size);
        ESP_LOGI(tag_stg, "\tstatus    = 0x%x, state = 0x%x, trend = 0x%x", status, state, trend);
        ESP_LOGI(tag_stg, "\tsequence  = 0x%x", sequence);
        ESP_LOGI(tag_stg, "\ttimestamp = 0x%x", timestamp);
        ESP_LOGI(tag_stg, "\tglucose   = %d", glucose);
        ESP_LOGI(tag_stg, "\treceived crc = 0x%02x, calculated crc = 0x%02x", crc, crc_calc);

        vRingbufferReturnItem(rbuf_handle, data);
        data = (uint8_t *)xRingbufferReceive(rbuf_handle, &item_size, pdMS_TO_TICKS(1000));
    }
}