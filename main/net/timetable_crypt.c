#include "timetable_crypt.h"

#include <string.h>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

static int hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg,
                       size_t msg_len, uint8_t out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == NULL) {
        return -1;
    }
    return mbedtls_md_hmac(md, key, key_len, msg, msg_len, out) == 0 ? 0 : -1;
}

/* No early exit, so the time taken does not say how much of the tag was
 * right. */
static int tags_differ(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= (uint8_t)(a[i] ^ b[i]);
    }
    return d != 0;
}

/* AES-CTR on top of the block cipher, rather than mbedtls_aes_crypt_ctr():
 * that one exists only when MBEDTLS_CIPHER_MODE_CTR is configured, and on
 * this build the AES context is ESP-IDF's hardware one. ECB is there in
 * every configuration. The counter is the whole 16-byte block, incremented
 * big-endian, which is what OpenSSL's aes-256-ctr does. */
static int aes_ctr(const uint8_t key[32], const uint8_t iv[16], uint8_t *data, size_t len)
{
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int rc = mbedtls_aes_setkey_enc(&aes, key, 256);

    uint8_t ctr[16], stream[16];
    memcpy(ctr, iv, sizeof ctr);
    for (size_t off = 0; rc == 0 && off < len; off += 16) {
        rc = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, ctr, stream);
        size_t chunk = (len - off < 16) ? len - off : 16;
        for (size_t i = 0; i < chunk; i++) {
            data[off + i] ^= stream[i];
        }
        for (int i = 15; i >= 0; i--) {
            if (++ctr[i] != 0) {
                break;
            }
        }
    }
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(stream, sizeof stream);
    return rc == 0 ? 0 : -1;
}

int timetable_decrypt(const uint8_t key[TT_KEY_LEN], uint8_t *buf, size_t len)
{
    if (key == NULL || buf == NULL || len < TT_OVERHEAD) {
        return TT_CRYPT_ERR_SHORT;
    }
    if (memcmp(buf, TT_MAGIC, TT_MAGIC_LEN) != 0) {
        return TT_CRYPT_ERR_MAGIC;
    }

    static const char enc_label[] = "esp-flight-monitor timetable 2 enc";
    static const char mac_label[] = "esp-flight-monitor timetable 2 mac";
    uint8_t enc_key[32], mac_key[32], tag[32];
    int rc = TT_CRYPT_ERR_CRYPTO;

    if (hmac_sha256(key, TT_KEY_LEN, (const uint8_t *)enc_label, sizeof enc_label - 1,
                    enc_key) != 0 ||
        hmac_sha256(key, TT_KEY_LEN, (const uint8_t *)mac_label, sizeof mac_label - 1,
                    mac_key) != 0) {
        goto out;
    }

    size_t body = len - TT_TAG_LEN;
    if (hmac_sha256(mac_key, sizeof mac_key, buf, body, tag) != 0) {
        goto out;
    }
    if (tags_differ(tag, buf + body, TT_TAG_LEN)) {
        rc = TT_CRYPT_ERR_TAG;
        goto out;
    }

    uint8_t *ct     = buf + TT_MAGIC_LEN + TT_IV_LEN;
    size_t   ct_len = body - TT_MAGIC_LEN - TT_IV_LEN;
    if (aes_ctr(enc_key, buf + TT_MAGIC_LEN, ct, ct_len) != 0) {
        goto out;
    }
    memmove(buf, ct, ct_len);
    buf[ct_len] = '\0';           /* inside the buffer: the tag was after it */
    rc = (int)ct_len;

out:
    mbedtls_platform_zeroize(enc_key, sizeof enc_key);
    mbedtls_platform_zeroize(mac_key, sizeof mac_key);
    return rc;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int timetable_key_from_hex(const char *hex, uint8_t key[TT_KEY_LEN])
{
    if (hex == NULL || key == NULL || strlen(hex) != 2 * TT_KEY_LEN) {
        return -1;
    }
    for (int i = 0; i < TT_KEY_LEN; i++) {
        int hi = hex_nibble(hex[2 * i]), lo = hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            mbedtls_platform_zeroize(key, TT_KEY_LEN);
            return -1;
        }
        key[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}
