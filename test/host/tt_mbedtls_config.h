/* The smallest mbedtls the host suite needs: AES and HMAC-SHA256 for
 * main/net/timetable_crypt.c (D95). The firmware uses ESP-IDF's own, full
 * configuration; this one only has to build the same algorithms from the
 * same sources, so test_timetable.c can open the file the CI selftest writes. */
#define MBEDTLS_AES_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_MD_C
