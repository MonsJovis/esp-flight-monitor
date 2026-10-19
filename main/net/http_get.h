/* Thin wrapper over esp_http_client, for the flight data path.
 *
 * Plain http:// and https:// both work. The data path was plain HTTP only,
 * on purpose (AGENTS.md §4: no handshake, roughly 40 KB more free heap per
 * connection), until adsb.lol started answering http:// with a 301 to
 * https:// on 2026-10-19 and the panel lost every aircraft (D92). By then the
 * memory argument had already gone: OTA brought the Mozilla bundle in (D72)
 * and its record buffers live in PSRAM (sdkconfig.defaults), so an https://
 * URL here costs one handshake and no internal heap. A host that still
 * answers plain HTTP (adsb.im's routeset, Open-Meteo) keeps its http:// URL:
 * there is no reason to pay for a handshake nobody asked for.
 *
 * Buffers are caller-supplied and fixed size; nothing in this file grows the
 * heap unboundedly. A 30 nm poll payload is ~4-8 KB (AGENTS.md §6), so a
 * 16 KB caller buffer is the typical choice.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sent with contact info on every request (AGENTS.md §7 — planespotters.net
 * rejects generic User-Agents outright, and it's the courteous thing to do
 * with a free community service). Deliberately carries no email address. */
#define HTTP_USER_AGENT "esp-flight-monitor/0.1 (+https://github.com/monsjovis/esp-flight-monitor)"

/* Negative return codes, distinct from any HTTP status. */
#define HTTP_ERR_INVAL    (-1)  /* bad arguments                              */
#define HTTP_ERR_CONN     (-2)  /* could not open/complete the connection     */
#define HTTP_ERR_TIMEOUT  (-3)  /* request timed out                         */

/* Performs a GET. An https:// URL is verified against the same certificate
 * bundle OTA uses; an http:// URL stays plain. The response body is copied
 * into buf, always NUL-terminated (so the usable capacity is buf_sz - 1).
 * Redirects are NEVER followed automatically — a caller that wants to react
 * to a 3xx gets the raw status and decides for itself. This matters
 * concretely on this project: adsb.lol emits a spurious 308 while throttling
 * (treat as throttling, not a redirect), and a host that moves to https:// is
 * a URL to change in the source, not a hop to chase on every poll — a
 * followed 301 would hide the move until the day the old URL stops
 * answering at all. (AGENTS.md §5, §7; D92.)
 *
 * Returns:
 *   >= 0              number of body bytes written into buf (excl. the NUL)
 *   HTTP_ERR_INVAL     url/buf is NULL, or buf_sz == 0
 *   HTTP_ERR_CONN      could not connect / DNS / transport error
 *   HTTP_ERR_TIMEOUT   timed out before completion
 *
 * *out_status is set to the HTTP status code whenever the return value is
 * >= 0 (i.e. the request completed and produced a response); left
 * unmodified on a negative return. May be NULL if the caller does not need it.
 *
 * *out_truncated (may be NULL) is set to true if the real response was
 * larger than buf_sz - 1: the body is truncated safely (never overflowed),
 * and the caller is told so rather than silently getting a partial parse.
 */
int http_get(const char *url, char *buf, size_t buf_sz, int timeout_ms,
             int *out_status, bool *out_truncated);

/* Same contract as http_get(), but POSTs `body` (NUL-terminated) with
 * Content-Type: application/json. Used for the batched
 * adsb.im/api/0/routeset request (AGENTS.md §4). */
int http_post_json(const char *url, const char *body, char *buf, size_t buf_sz,
                    int timeout_ms, int *out_status, bool *out_truncated);

#ifdef __cplusplus
}
#endif
