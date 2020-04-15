#include "dexcom_g6_reader.h"

#define BUFFER_SIZE         256
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
    UBaseType_t res = xRingbufferSend(rbuf_handle, in, length, pdMS_TO_TICKS(1000));
    if(res != pdTRUE) {
        ESP_LOGE(tag_stg, "Error while writing into ringbuffer.");
    }
}

//TODO: print debug output
//TODO: backfill