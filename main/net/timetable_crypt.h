/* timetable_crypt.h — opens the encrypted timetable CI publishes (D95).
 *
 * WHY IT IS ENCRYPTED. The pairs come from AeroDataBox, whose terms forbid
 * passing their data to third parties (§5.8 of their Terms of Use), and the
 * file sits on a public branch of a public repo. Encrypted, it is readable by
 * the panels that hold the key and by nobody else, which is the difference
 * between serving our own users and redistributing their data.
 *
 * THE FORMAT, written by tools/build_timetable.py:
 *
 *   "ESPFMTT2" | IV, 16 bytes | ciphertext | tag, 32 bytes
 *
 *   ciphertext = AES-256-CTR(enc_key, IV) of the plaintext timetable
 *                (128-bit big-endian counter, which is OpenSSL's aes-256-ctr)
 *   tag        = HMAC-SHA256(mac_key, magic | IV | ciphertext)
 *   enc_key    = HMAC-SHA256(key, "esp-flight-monitor timetable 2 enc")
 *   mac_key    = HMAC-SHA256(key, "esp-flight-monitor timetable 2 mac")
 *
 * Encrypt-then-MAC with two keys derived from one. The 32-byte key lives in
 * the repo's Actions secrets and in each panel's NVS, and nowhere else
 * (tools/provision.py --timetable-key).
 *
 * Only mbedtls, no ESP-IDF headers, so test/host builds it against the same
 * mbedtls sources and decrypts the file the CI selftest writes.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define TT_KEY_LEN        32
#define TT_MAGIC          "ESPFMTT2"
#define TT_MAGIC_LEN      8
#define TT_IV_LEN         16
#define TT_TAG_LEN        32
#define TT_OVERHEAD       (TT_MAGIC_LEN + TT_IV_LEN + TT_TAG_LEN)

#define TT_CRYPT_ERR_SHORT   (-1)   /* smaller than the container itself     */
#define TT_CRYPT_ERR_MAGIC   (-2)   /* not this format: a 404 page, a v1 CSV */
#define TT_CRYPT_ERR_TAG     (-3)   /* wrong key, or the bytes were changed  */
#define TT_CRYPT_ERR_CRYPTO  (-4)   /* mbedtls refused                       */

/* Checks the tag, then decrypts in place: on success the plaintext starts at
 * buf[0], is followed by a NUL, and its length is returned. Nothing is
 * decrypted unless the tag is right, so a wrong key or a damaged download is
 * an error, never garbage handed to the parser. */
int timetable_decrypt(const uint8_t key[TT_KEY_LEN], uint8_t *buf, size_t len);

/* "a3f0..." (64 hex digits, either case) -> 32 bytes. 0 on success, -1 when
 * it is not exactly that. For the console that stores the key. */
int timetable_key_from_hex(const char *hex, uint8_t key[TT_KEY_LEN]);
