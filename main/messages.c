#include <stdint-gcc.h>
#include "host/ble_uuid.h"
#include "esp_system.h"
#include "mbedtls/aes.h"
#include "esp32/rom/crc.h"

#include "dexcom_g6_reader.h"



const char* tag_msg = "[Dexcom-G6-Reader][msg]";
unsigned char token_bytes[8];
unsigned char enc_token_bytes[8];
unsigned char challenge_bytes[8];
uint8_t authentication_status = 0;
uint8_t bond_status = 0;
mbedtls_aes_context aes_ecb_ctx;
unsigned char key[16];
uint32_t backfill_start_time;
uint32_t backfill_end_time;
uint8_t next_backfill_sequence = 1;
uint8_t backfill_buffer[500];
uint32_t backfill_buffer_pos = 0;
bool expecting_backfill = false;

/**
 * Encrypts input bytes with aes-128-ecb.
 *
 * @param in_bytes      Input bytes
 * @param out_bytes     Output bytes
 */
void
dgr_encrypt(const unsigned char in_bytes[8], unsigned char out_bytes[8]) {
    unsigned char aes_in[16];
    unsigned char aes_out[16];
    int rc;

    for(int i = 0; i < 8; i++) {
        aes_in[i] = in_bytes[i];
        aes_in[i + 8] = in_bytes[i];
    }

    rc = mbedtls_aes_crypt_ecb(&aes_ecb_ctx, MBEDTLS_AES_ENCRYPT, aes_in, aes_out);
    if(rc != 0) {
        ESP_LOGE(tag_msg, "Error while encrypting. rc = 0x%04x", rc);
        dgr_error();
    }

    //ESP_LOGI(tag_msg, "AES INPUT:");
    //ESP_LOG_BUFFER_HEX_LEVEL(tag_msg, aes_in, 16, ESP_LOG_INFO);
    //ESP_LOGI(tag_msg, "AES_OUTPUT:");
    //ESP_LOG_BUFFER_HEX_LEVEL(tag_msg, aes_out, 16, ESP_LOG_INFO);

    for(int i = 0; i < 8; i++) {
        out_bytes[i] = aes_out[i];
    }
}

/*****************************************************************************
 *  outgoing message building                                                *
 *****************************************************************************/

void
dgr_build_auth_request_msg(struct os_mbuf *om) {
    uint8_t msg[10];
    uint8_t rnd;
    unsigned int i;
    int rc;

    if(om) {
        for(i = 0; i < 8; i++) {
            rnd = esp_random() % 256;

            msg[i + 1] = rnd;
            token_bytes[i] = rnd;
        }

        dgr_encrypt(token_bytes, enc_token_bytes);

        msg[0] = AUTH_REQUEST_TX_OPCODE;
        // alt bt channel
        msg[9] = 0x1;
        // std bt channel
        //msg[0] = 0x2;

        rc = os_mbuf_copyinto(om, 0, msg, 10);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = 0x%04x", rc);
            dgr_error();
        }

        ESP_LOGI(tag_msg, "AuthRequest message: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
            msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8], msg[9]);
    }
}

void
dgr_build_auth_challenge_msg(struct os_mbuf *om) {
    uint8_t msg[9];
    unsigned char enc_challenge[8];
    int rc;

    if(om) {
        msg[0] = AUTH_CHALLENGE_TX_OPCODE;

        dgr_encrypt(challenge_bytes, enc_challenge);

        for(int i = 0; i < 8; i++) {
            msg[i + 1]  = enc_challenge[i];
        }

        ESP_LOGI(tag_msg, "challenge           :");
        ESP_LOG_BUFFER_HEX_LEVEL(tag_msg, challenge_bytes, 8, ESP_LOG_INFO);
        ESP_LOGI(tag_msg, "encrypted challenge :");
        ESP_LOG_BUFFER_HEX_LEVEL(tag_msg, enc_challenge, 8, ESP_LOG_INFO);
        rc = os_mbuf_copyinto(om, 0, msg, 9);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = 0x%04x", rc);
            dgr_error();
        }
    }
}

void
dgr_build_keep_alive_msg(struct os_mbuf *om, uint8_t time) {
    uint8_t msg[2];
    int rc;

    if(om) {
        msg[0] = KEEP_ALIVE_TX_OPCODE;
        msg[1] = time;

        rc = os_mbuf_copyinto(om, 0, msg, 2);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = 0x%04x", rc);
            dgr_error();
        }
    }
}

void
dgr_build_bond_request_msg(struct os_mbuf *om) {
    uint8_t msg[1] = {BOND_REQUEST_TX_OPCODE};
    int rc;

    if(om) {
        rc = os_mbuf_copyinto(om, 0, msg, 1);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = 0x%04x", rc);
            dgr_error();
        }
    }
}

void
dgr_build_glucose_tx_msg(struct os_mbuf *om) {
    uint8_t msg[3];
    uint16_t crc;
    int rc;

    msg[0] = GLUCOSE_TX_OPCODE;
    crc = ~crc16_be((uint16_t)~0x0000, msg, 1);
    // write crc as little-endian
    msg[1] = crc;
    msg[2] = crc >> 8U;

    if(om) {
        rc = os_mbuf_copyinto(om, 0, msg, 3);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = 0x%04x", rc);
            dgr_error();
        }
    }
}

void
dgr_build_backfill_tx_msg(struct os_mbuf *om) {
    uint8_t msg[20] = {0};
    uint16_t crc;
    int rc;

    msg[0] = BACKFILL_TX_OPCODE;
    msg[1] = 0x5;
    msg[2] = 0x2;
    msg[3] = 0x0;

    // start time
    write_u32_le(&msg[4], backfill_start_time);

    // end time
    write_u32_le(&msg[8], backfill_end_time);

    // crc
    crc = ~crc16_be((uint16_t)~0x0000, msg, 18);
    write_u16_le(&msg[18], crc);

    ESP_LOGI(tag_msg, "BackfillTx : requesting backfill from %x to %x",
        backfill_start_time, backfill_end_time);

    if(om) {
        rc = os_mbuf_copyinto(om, 0, msg, 20);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = 0x%04x", rc);
            dgr_error();
        }
    }
}

void
dgr_build_time_tx_msg(struct os_mbuf *om) {
    uint8_t msg[3];
    uint16_t crc;
    int rc;

    msg[0] = TIME_TX_OPCODE;
    crc = ~crc16_be((uint16_t)~0x0000, msg, 1);
    // crc as little endian
    msg[1] = crc;
    msg[2] = crc >> 8U;

    if(om) {
        rc = os_mbuf_copyinto(om, 0, msg, 3);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = 0x%04x", rc);
            dgr_error();
        }
    }
}

/*****************************************************************************
 *  incoming message parsing                                                 *
 *****************************************************************************/

void
dgr_parse_auth_challenge_msg(const uint8_t *data, uint8_t length, bool *correct_token) {
    if(length == 17) {
        for(int i = 0; i < 8; i++) {
            challenge_bytes[i] = data[i + 9];

            *correct_token = *correct_token && (data[i + 1] == enc_token_bytes[i]);
        }
    } else {
        ESP_LOGE(tag_msg, "Received AuthChallenge message has wrong length(%d).", length);
        dgr_error();
    }
}

void
dgr_parse_auth_status_msg(const uint8_t *data, uint8_t length) {
    if(length == 3) {
        authentication_status = data[1];
        bond_status = data[2];

        ESP_LOGI(tag_msg, "[04] AuthStatus: auth = %d, bond = %d", authentication_status, bond_status);
    } else {
        ESP_LOGE(tag_msg, "Received AuthStatus message has wrong length(%d).", length);
        dgr_error();
    }
}

void
dgr_parse_glucose_msg(const uint8_t *data, uint8_t length, uint8_t conn_handle) {
    if(length >= 16) {
        uint8_t transmitter_state = data[1];
        uint32_t sequence = make_u32_from_bytes_le(&data[2]);
        uint32_t timestamp = make_u32_from_bytes_le(&data[6]);
        uint16_t glucose = make_u16_from_bytes_le(&data[10]) & 0xfffU;
        uint8_t calibration_state = data[12];
        uint8_t trend = data[13];
        uint16_t crc = make_u16_from_bytes_le(&data[length - 2]);
        uint16_t crc_calc = ~crc16_be((uint16_t)~0x0000, data, length - 2);

        ESP_LOGI(tag_msg, "[=========== GlucoseRx ===========]");
        ESP_LOGI(tag_msg, "\ttransmitter state = %s (0x%x)", translate_transmitter_state(transmitter_state),
            transmitter_state);
        ESP_LOGI(tag_msg, "\tcalibration state = %s (0x%x)", translate_calibration_state(calibration_state),
                 calibration_state);
        ESP_LOGI(tag_msg, "\tsequence  = 0x%x", sequence);
        ESP_LOGI(tag_msg, "\ttimestamp = 0x%x", timestamp);
        ESP_LOGI(tag_msg, "\tglucose   = %d", glucose);
        ESP_LOGI(tag_msg, "\ttrend     = 0x%x", trend);
        ESP_LOGI(tag_msg, "\treceived crc = 0x%02x, calculated crc = 0x%02x", crc, crc_calc);

        if(last_sequence - sequence == 0) {
            ESP_LOGE(tag_msg, "Duplicate Reading.");
            dgr_error();
        } else if(sequence < last_sequence) {
            ESP_LOGE(tag_msg, "Out of Band Reading. last_sequence = %d, sequence = %d",
                     last_sequence, sequence);
            dgr_error();
        }

        if(crc != crc_calc) {
            ESP_LOGE(tag_msg, "GlucoseRx : Calculated CRC does not match received CRC.");
            dgr_error();
        }

        if(calibration_state != CALIB_STATE_OK) {
            ESP_LOGE(tag_msg, "GlucoseRx : Transmitter is not in OK state. state = %s (0x%02x)",
                translate_calibration_state(calibration_state), calibration_state);
            dgr_error();
        }

        dgr_save_to_ringbuffer(timestamp, glucose, calibration_state, trend);
        dgr_check_for_backfill_and_sleep(conn_handle, sequence);
    } else {
        ESP_LOGE(tag_msg, "Received GlucoseRx message has wrong length(%d).", length);
        dgr_error();
    }
}

void
dgr_parse_backfill_status_msg(const uint8_t *data, uint8_t length) {
    if(length == 20) {
        uint8_t status = data[1];
        uint8_t unknown_1 = data[2];
        uint8_t unknown_2 = data[3];
        uint32_t start_time = make_u32_from_bytes_le(&data[4]);
        uint32_t end_time = make_u32_from_bytes_le(&data[8]);
        uint16_t crc = make_u16_from_bytes_le(&data[18]);
        uint16_t crc_calc = ~crc16_be((uint16_t)~0x0000, data, length - 2);

        ESP_LOGI(tag_msg, "[=========== BackfillRx ===========]");
        ESP_LOGI(tag_msg, "\tstatus = 0x%x", status);
        ESP_LOGI(tag_msg, "\tstart_time   = 0x%x", start_time);
        ESP_LOGI(tag_msg, "\tend_time     = 0x%x", end_time);
        ESP_LOGI(tag_msg, "\treceived crc = 0x%x", crc);
        ESP_LOGI(tag_msg, "\tcalculated crc = 0x%x", crc_calc);

        expecting_backfill = true;
    } else {
        ESP_LOGE(tag_msg, "Received Backfill status message has wrong length(%d).", length);
        dgr_error();
    }
}

void
dgr_parse_time_msg(const uint8_t *data, uint8_t length, uint16_t conn_handle) {
    if(length == 16) {
        uint8_t state = data[1];
        // seconds since transmitter start
        uint32_t current_time = make_u32_from_bytes_le(&data[2]);
        // seconds since session start
        uint32_t session_start_time = make_u32_from_bytes_le(&data[6]);

        ESP_LOGI(tag_msg, "TransmitterTimeRx (state = %d)", state);
        ESP_LOGI(tag_msg, "\tcurrent time       = 0x%x", current_time);
        ESP_LOGI(tag_msg, "\tsession start time = 0x%x", session_start_time);

        // set backfill related times
        backfill_start_time = current_time - (60*30); // 30 mins before
        backfill_end_time = current_time - 60; // one minute before

        dgr_send_glucose_tx_msg(conn_handle);
    } else {
        ESP_LOGE(tag_msg, "Received Time message has wrong length(%d).", length);
        dgr_error();
    }
}

void
dgr_parse_backfill_data_msg(const uint8_t *data, const uint8_t length) {
    if(length > 2) {
        uint8_t sequence = data[0];
        uint8_t identifier = data[1];

        if(sequence == next_backfill_sequence) {
            next_backfill_sequence++;

            if(sequence == 1) {
                uint16_t request_counter = make_u16_from_bytes_le(&data[2]);
                uint16_t unknown = make_u16_from_bytes_le(&data[4]);
                ESP_LOGI(tag_msg, "Backfill:");
                ESP_LOGI(tag_msg, "\trequest counter = %d", request_counter);

                ESP_LOGI(tag_msg, "Added these bytes to the backfill buffer:");
                ESP_LOG_BUFFER_HEX_LEVEL(tag_msg, &data[6], length - 6, ESP_LOG_INFO);
                memcpy(&backfill_buffer[backfill_buffer_pos], &data[6], length - 6);
                backfill_buffer_pos += length - 6;
            } else {
                ESP_LOGI(tag_msg, "Added these bytes to the backfill buffer:");
                ESP_LOG_BUFFER_HEX_LEVEL(tag_msg, &data[2], length - 2, ESP_LOG_INFO);
                memcpy(&backfill_buffer[backfill_buffer_pos], &data[2], length - 2);
                backfill_buffer_pos += length - 2;
            }
        } else {
            ESP_LOGE(tag_msg, "Received out-of-order Backfill data which is not supported.");
            dgr_error();
        }
    } else {
        ESP_LOGE(tag_msg, "Received Backfill data message has wrong length(%d).", length);
        dgr_error();
    }
}


/*****************************************************************************
 *  message util functions                                                   *
 *****************************************************************************/

void
dgr_create_mbuf_pool() {
    int rc;

    rc = os_mempool_init(&dgr_mbuf_mempool, MBUF_NUM_MBUFS,
        MBUF_MEMBLOCK_SIZE, &dgr_mbuf_buffer[0], "mbuf_pool");
    if(rc != 0) {
        ESP_LOGE(tag_msg, "Error while initializing os_mempool. rc = 0x%04x", rc);
        dgr_error();
    }

    rc = os_mbuf_pool_init(&dgr_mbuf_pool, &dgr_mbuf_mempool, MBUF_MEMBLOCK_SIZE,
        MBUF_NUM_MBUFS);
    if(rc != 0) {
        ESP_LOGE(tag_msg, "Error while initializing os_mbuf_pool. rc = 0x%04x", rc);
        dgr_error();
    }
}

void
dgr_create_crypto_context() {
    key[0] = 0x30;
    key[1] = 0x30;
    key[8] = 0x30;
    key[9] = 0x30;

    for(int i = 0; i < 6; i++) {
        key[i + 2] = transmitter_id[i];
        key[i + 10] = transmitter_id[i];
    }

    ESP_LOGI(tag_msg, "AES key :");
    ESP_LOG_BUFFER_HEXDUMP(tag_msg, key, 16, ESP_LOG_INFO);

    mbedtls_aes_init(&aes_ecb_ctx);
    mbedtls_aes_setkey_enc(&aes_ecb_ctx, key, 128);
    mbedtls_aes_setkey_dec(&aes_ecb_ctx, key, 128);
}

void
dgr_print_token_details() {
    ESP_LOGI(tag_msg, "token:");
    ESP_LOGI(tag_msg, "\t%02x %02x %02x %02x %02x %02x %02x %02x",
        token_bytes[0], token_bytes[1], token_bytes[2], token_bytes[3],
        token_bytes[4], token_bytes[5], token_bytes[6], token_bytes[7]);

    ESP_LOGI(tag_msg, "encrypted token:");
    ESP_LOGI(tag_msg, "\t%02x %02x %02x %02x %02x %02x %02x %02x",
        enc_token_bytes[0], enc_token_bytes[1], enc_token_bytes[2], enc_token_bytes[3],
        enc_token_bytes[4], enc_token_bytes[5], enc_token_bytes[6], enc_token_bytes[7]);

    ESP_LOGI(tag_msg, "challenge bytes:");
    ESP_LOGI(tag_msg, "\t%02x %02x %02x %02x %02x %02x %02x %02x",
        challenge_bytes[0], challenge_bytes[1], challenge_bytes[2], challenge_bytes[3],
        challenge_bytes[4], challenge_bytes[5], challenge_bytes[6], challenge_bytes[7]);
}