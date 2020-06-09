#include <esp_sleep.h>
#include "dexcom_g6_reader.h"
#include "esp32/rom/crc.h"

#define BUFFER_SIZE         420     // 28 bytes * 15
#define BUFFER_TYPE         RINGBUF_TYPE_NOSPLIT
RTC_DATA_ATTR StaticRingbuffer_t buffer_struct;
RTC_DATA_ATTR uint8_t buffer_storage[BUFFER_SIZE];
RTC_DATA_ATTR RingbufHandle_t rbuf_handle;
RTC_DATA_ATTR uint32_t last_sequence = 0;

static const char *tag_stg = "[Dexcom-G6-Reader][storage]";

void
dgr_init_ringbuffer() {
    rbuf_handle = xRingbufferCreateStatic(BUFFER_SIZE, BUFFER_TYPE, (uint8_t*)&buffer_storage, &buffer_struct);
}

/**
 * Saves the given values in the ringbuffer
 *
 * @param timestamp             Timestamp of a glucose reading
 * @param glucose               Glucose value of a reading
 * @param calibration_state     Calibration state of a reading
 * @param trend                 Trend value of a reading
 */
void
dgr_save_to_ringbuffer(uint32_t timestamp, uint16_t glucose, uint8_t calibration_state, uint8_t trend) {
    size_t free_size = xRingbufferGetCurFreeSize(rbuf_handle);
    // 8 bytes data + 8 byte header
    if(free_size >= 16) {
        uint8_t in[8];
        write_u32_le(in, timestamp);
        write_u16_le(&in[4], glucose);
        in[6] = calibration_state;
        in[7] = trend;

        UBaseType_t res = xRingbufferSend(rbuf_handle, in, 8, pdMS_TO_TICKS(5000));

        if (res != pdTRUE) {
            ESP_LOGE(tag_stg, "Error while writing into ringbuffer. rc = 0x%04x", res);
            dgr_error();
        }
    } else {
        ESP_LOGE(tag_stg, "Ringbuffer is full. Printing debug info.");
        dgr_print_rbuf(false);
        //TODO: maybe do something more useful
    }
}

/**
 * After a glucose reading was received, this function checks if backfill is needed.
 *
 * @param conn_handle           Connection handle
 * @param sequence              Sequence number of the last glucose reading
 */
void
dgr_check_for_backfill_and_sleep(uint16_t conn_handle, uint32_t sequence) {
    // dont do backfill after the first reading
    uint32_t sequence_diff = last_sequence == 0 ? 0 : sequence - last_sequence;
    last_sequence = sequence;


    if(sequence_diff == 1) {
        // tear down bt?
        ESP_LOGI(tag_stg, "No Backfill necessary. Going to deep sleep for %d seconds", SLEEP_BETWEEN_READINGS);
        dgr_print_rbuf(true);
        esp_deep_sleep(SLEEP_BETWEEN_READINGS * 1000000); // time is in microseconds
    } else if(sequence_diff > 1 || sequence_diff == 0) {
        // enable backfill notifications
        ESP_LOGI(tag_stg, "Sequence difference is : %d. Starting backfill.", sequence_diff);
        dgr_enable_server_side_updates_msg(conn_handle, &backfill_uuid.u, dgr_send_backfill_enable_notif_cb, 2);
    } else {
        ESP_LOGE(tag_stg, "Unexpected difference between sequences : %d", sequence_diff);
        dgr_error();
    }

}

/**
 * After all backfill data is received, parse and save all of it in the ringbuffer.
 */
void
dgr_parse_backfill() {
    int i = 0;

    ESP_LOGI(tag_stg, "Starting to parse Backfill data.");
    while(i < backfill_buffer_pos) {
        uint32_t timestamp = make_u32_from_bytes_le(&backfill_buffer[i]);
        uint16_t glucose = make_u16_from_bytes_le(&backfill_buffer[i + 4]);
        uint8_t calibration_state = backfill_buffer[i + 6];
        uint8_t trend = backfill_buffer[i + 7];

        ESP_LOGI(tag_stg, "[=========== Backfill Data ===========]");
        ESP_LOGI(tag_stg, "\ttimestamp         = 0x%x", timestamp);
        ESP_LOGI(tag_stg, "\tglucose           = %d", glucose);
        ESP_LOGI(tag_stg, "\tcalibration state = %s (0x%x)",
            translate_calibration_state(calibration_state), calibration_state);
        ESP_LOGI(tag_stg, "\ttrend             = 0x%x", trend);
        i += 8;

        dgr_save_to_ringbuffer(timestamp, glucose, calibration_state, trend);
    }
}

/**
 * Prints content of the ringbuffer for debug purposes.
 *
 * @param keep_items            true if all ringbuffer items should be kept in the ringbuffer,
 *                              false if ringbuffer items should be discarded after output
 */
void
dgr_print_rbuf(bool keep_items) {
    uint8_t buffer_save[BUFFER_SIZE];
    int i = 0;

    size_t item_size;
    uint8_t *data = (uint8_t *)xRingbufferReceive(rbuf_handle, &item_size, pdMS_TO_TICKS(1000));

    xRingbufferPrintInfo(rbuf_handle);
    while(data != NULL) {
        uint32_t timestamp = make_u32_from_bytes_le(data);
        uint16_t glucose = make_u16_from_bytes_le(&data[4]);
        uint8_t calibration_state = data[6];
        uint8_t trend = data[7];

        ESP_LOGI(tag_stg, "[=========== RingbufItem (size=%d) ===========]", item_size);
        ESP_LOGI(tag_stg, "\ttimestamp = 0x%x", timestamp);
        ESP_LOGI(tag_stg, "\tglucose   = %d", glucose);
        ESP_LOGI(tag_stg, "\tcalibration state = %s",
                 translate_calibration_state(calibration_state));
        ESP_LOGI(tag_stg, "\ttrend             = 0x%x", trend);

        if(keep_items) {
            memcpy(&buffer_save[i++ * item_size], data, item_size);
        }
        vRingbufferReturnItem(rbuf_handle, data);
        data = (uint8_t *)xRingbufferReceive(rbuf_handle, &item_size, pdMS_TO_TICKS(1000));
    }

    // resave items
    if(keep_items) {
        for (int j = 0; j < i; j++) {
            UBaseType_t res = xRingbufferSend(rbuf_handle, &buffer_save[j * item_size], 8, pdMS_TO_TICKS(5000));

            if (res != pdTRUE) {
                ESP_LOGE(tag_stg, "Error while writing into ringbuffer. rc = 0x%04x", res);
                dgr_error();
            }
        }
    }
}