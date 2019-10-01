#include "host/ble_uuid.h"
#include "esp_system.h"
#include "mbedtls/aes.h"

#include "dexcom_g6_reader.h"

const char* tag_msg = "[Dexcom-G6-Reader][msg]";
unsigned char token_bytes[8];
unsigned char challenge_bytes[8];
uint8_t authentication_status = 0;
uint8_t bond_status = 0;
mbedtls_aes_context aes_ecb_ctx;
const unsigned char key[] = "0081234500812345";
// TODO: define key through sensor_id

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

// Authentication Request
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

        msg[0] = AUTH_REQUEST_TX_OPCODE;
        msg[9] = 0x2;

        rc = os_mbuf_copyinto(om, 0, msg, 10);
        if(rc != 0) {
            ESP_LOGE(tag_msg, "Error while copying into mbuf. rc = %d", rc);
        }
    }
}

// Authentication challenge
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
dgr_parse_auth_challenge_msg(uint8_t *data, uint8_t length) {
    if(length == 17) {
        // TODO: check encrypted token bytes
        for(int i = 0; i < 8; i++) {
            challenge_bytes[i] = data[i + 9];
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
 *  message util functions                                                           *
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
}