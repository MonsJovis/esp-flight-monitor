#include "http_get.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http_get";

typedef struct {
    char  *buf;
    size_t buf_sz;    /* total capacity, including room for the NUL */
    size_t written;
    bool   truncated;
} recv_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }

    recv_ctx_t *ctx = (recv_ctx_t *)evt->user_data;
    if (ctx == NULL || ctx->buf == NULL || ctx->buf_sz == 0 || evt->data_len <= 0) {
        return ESP_OK;
    }

    /* Leave room for the terminating NUL: never write into buf[buf_sz - 1]. */
    size_t space = (ctx->buf_sz - 1 > ctx->written) ? (ctx->buf_sz - 1 - ctx->written) : 0;
    size_t n = (size_t)evt->data_len;
    if (n > space) {
        n = space;
        ctx->truncated = true;
    }
    if (n > 0) {
        memcpy(ctx->buf + ctx->written, evt->data, n);
        ctx->written += n;
    }
    return ESP_OK;
}

/* Shared by http_get() / http_post_json(): builds the client, performs the
 * request synchronously, copies the body into buf, and cleans up. `body`
 * (POST only) may be NULL for a GET. */
static int do_request(const char *url, esp_http_client_method_t method,
                       const char *body, char *buf, size_t buf_sz, int timeout_ms,
                       int *out_status, bool *out_truncated)
{
    if (url == NULL || buf == NULL || buf_sz == 0) {
        return HTTP_ERR_INVAL;
    }

    recv_ctx_t ctx = { .buf = buf, .buf_sz = buf_sz, .written = 0, .truncated = false };

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = timeout_ms,
        .user_agent = HTTP_USER_AGENT,
        .event_handler = http_event_handler,
        .user_data = &ctx,
        /* Never auto-follow a redirect. adsb.lol's spurious 308 must be read
         * as throttling, and a genuine 3xx (e.g. adsb.fi's plain-HTTP ->
         * https:// bounce) must be surfaced as the status it is, not
         * silently chased into a TLS connection this project does not carry
         * the stack for (AGENTS.md §5, §7). */
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return HTTP_ERR_CONN;
    }

    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, body != NULL ? (int)strlen(body) : 0);
    }

    buf[0] = '\0';
    esp_err_t err = esp_http_client_perform(client);

    int ret;
    if (err == ESP_OK) {
        buf[ctx.written] = '\0';
        if (out_status != NULL) {
            *out_status = esp_http_client_get_status_code(client);
        }
        if (out_truncated != NULL) {
            *out_truncated = ctx.truncated;
        }
        if (ctx.truncated) {
            ESP_LOGW(TAG, "response truncated: buffer holds %u of the real body",
                     (unsigned)ctx.written);
        }
        ret = (int)ctx.written;
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "%s timed out (%d ms): %s", url, timeout_ms, esp_err_to_name(err));
        ret = HTTP_ERR_TIMEOUT;
    } else {
        ESP_LOGW(TAG, "%s failed: %s", url, esp_err_to_name(err));
        ret = HTTP_ERR_CONN;
    }

    esp_http_client_cleanup(client);
    return ret;
}

int http_get(const char *url, char *buf, size_t buf_sz, int timeout_ms,
             int *out_status, bool *out_truncated)
{
    return do_request(url, HTTP_METHOD_GET, NULL, buf, buf_sz, timeout_ms,
                       out_status, out_truncated);
}

int http_post_json(const char *url, const char *body, char *buf, size_t buf_sz,
                    int timeout_ms, int *out_status, bool *out_truncated)
{
    if (body == NULL) {
        return HTTP_ERR_INVAL;
    }
    return do_request(url, HTTP_METHOD_POST, body, buf, buf_sz, timeout_ms,
                       out_status, out_truncated);
}
