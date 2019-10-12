#include "host/ble_uuid.h"
#include "esp_system.h"
#include "mbedtls/aes.h"

#include "dexcom_g6_reader.h"

const char* tag_msg = "[Dexcom-G6-Reader][msg]";
unsigned char token_bytes[8];
unsigned char enc_token_bytes[8];
unsigned char challenge_bytes[8];
uint8_t authentication_status = 0;
uint8_t bond_status = 0;
mbedtls_aes_context aes_ecb_ctx;
unsigned char *key;

void
dgr_encrypt(unsigned char in_bytes[8], unsigned char out_bytes[8]) {
    unsigned char aes_in[16];
    unsigned char aes_out[16];

    for(int i = 0; i < 8; i++) {
        aes_in[i] = in_bytes[i];
        aes_in[i+8] = in_bytes[i];
    }

    mbedtls_aes_crypt_ecb(&aes_ecb_ctx, MBEDTLS_AES_ENCRYPT, aes_in, aes_out);

    for(int i = 0; i < 8; i++) {
        out_bytes[i] = aes_out[i];
    }
}

/*****************************************************************************
 *  Messages                                                                 *
 *****************************************************************************/

void
dgr_build_auth_request_msg(struct os_mbuf *om) {
    uint8_t msg[10];
    unsigned int random = esp_random();
    unsigned int i;
    int rc;

    if(om) {
        for(i = 0; i < 8; i++) {
            msg[i + 1] = random >> (i * 8);
            token_bytes[i] = random >> (i * 8);
        }

        dgr_encrypt(token_bytes, enc_token_bytes);

        msg[0] = AUTH_REQUEST_TX_OPCODE;
        msg[9] = 0x2;

        rc = os_mbuf_copyinto(om, 0, msg, 10);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = %d", rc);
        }
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

        rc = os_mbuf_copyinto(om, 0, msg, 9);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = %d", rc);
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
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = %d", rc);
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
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = %d", rc);
        }
    }
}


void
dgr_parse_auth_challenge_msg(uint8_t *data, uint8_t length, bool *correct_token) {
    if(length == 17) {
        for(int i = 0; i < 8; i++) {
            challenge_bytes[i] = data[i + 9];

            *correct_token = (data[i + 1] == enc_token_bytes[i]);
        }
    } else {
        ESP_LOGE(tag_msg, "Received AuthChallenge message has wrong length(%d).", length);
    }
}

void
dgr_parse_auth_status_msg(uint8_t *data, uint8_t length) {
    if(length == 3) {
        authentication_status = data[1];
        bond_status = data[2];
    } else {
        ESP_LOGE(tag_msg, "Received AuthStatus message has wrong length(%d).", length);
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
        ESP_LOGE(tag_msg, "Error while initializing os_mempool. rc = %d", rc);
    }

    rc = os_mbuf_pool_init(&dgr_mbuf_pool, &dgr_mbuf_mempool, MBUF_MEMBLOCK_SIZE,
        MBUF_NUM_MBUFS);
    if(rc != 0) {
        ESP_LOGE(tag_msg, "Error while initializing os_mbuf_pool. rc = %d", rc);
    }
}

void
dgr_create_crypto_context() {
    mbedtls_aes_init(&aes_ecb_ctx);
    mbedtls_aes_setkey_enc(&aes_ecb_ctx, key, 128);
    mbedtls_aes_setkey_dec(&aes_ecb_ctx, key, 128);

    for(int i = 0; i < 6; i++) {
        key[i + 2] = sensor_id[i];
        key[i + 10] = sensor_id[i];
    }
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