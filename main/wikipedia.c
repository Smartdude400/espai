#include "wikipedia.h"
#include "app_config.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "wiki";

#define WIKI_RESP_MAX  (16 * 1024)
#define WIKI_USER_AGENT "espai-esp32/1.0 (https://github.com/; hobby project)"

// --- growable response buffer ---------------------------------------------
typedef struct { char *buf; int len; int cap; } resp_acc_t;

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        resp_acc_t *acc = (resp_acc_t *)evt->user_data;
        if (acc && acc->buf && acc->len + evt->data_len < acc->cap) {
            memcpy(acc->buf + acc->len, evt->data, evt->data_len);
            acc->len += evt->data_len;
            acc->buf[acc->len] = '\0';
        }
    }
    return ESP_OK;
}

// GET url -> malloc'd body string (caller frees), or NULL on failure.
static char *http_get(const char *url)
{
    resp_acc_t acc = { .buf = malloc(WIKI_RESP_MAX), .len = 0, .cap = WIKI_RESP_MAX };
    if (!acc.buf) { ESP_LOGE(TAG, "OOM"); return NULL; }
    acc.buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_evt,
        .user_data = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 12000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", WIKI_USER_AGENT);
    esp_http_client_set_header(client, "Accept", "application/json");

    char *result = NULL;
    if (esp_http_client_perform(client) == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP %d, %d bytes", status, acc.len);
        if (status == 200 && acc.len > 0) { result = acc.buf; acc.buf = NULL; }
    } else {
        ESP_LOGW(TAG, "request failed");
    }
    esp_http_client_cleanup(client);
    free(acc.buf);   // NULL if we handed it off
    return result;
}

// Percent-encode everything except RFC3986 unreserved chars.
static void url_encode(const char *src, char *dst, size_t dst_sz)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 4 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = (char)c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0xF];
        }
    }
    dst[j] = '\0';
}

static bool parse_summary_json(const char *json, wiki_article_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    bool ok = false;
    const cJSON *title   = cJSON_GetObjectItemCaseSensitive(root, "title");
    const cJSON *extract = cJSON_GetObjectItemCaseSensitive(root, "extract");
    if (cJSON_IsString(extract) && extract->valuestring) {
        if (cJSON_IsString(title) && title->valuestring)
            strncpy(out->title, title->valuestring, sizeof(out->title) - 1);
        else out->title[0] = '\0';
        out->title[sizeof(out->title) - 1] = '\0';
        strncpy(out->extract, extract->valuestring, sizeof(out->extract) - 1);
        out->extract[sizeof(out->extract) - 1] = '\0';

        // Optional lead image: thumbnail.source
        out->image_url[0] = '\0';
        const cJSON *thumb = cJSON_GetObjectItemCaseSensitive(root, "thumbnail");
        if (cJSON_IsObject(thumb)) {
            const cJSON *src = cJSON_GetObjectItemCaseSensitive(thumb, "source");
            if (cJSON_IsString(src) && src->valuestring) {
                strncpy(out->image_url, src->valuestring, sizeof(out->image_url) - 1);
                out->image_url[sizeof(out->image_url) - 1] = '\0';
            }
        }
        ok = (out->extract[0] != '\0');
    }
    cJSON_Delete(root);
    return ok;
}

// Pull the best-matching page title out of an action=query&list=search response.
static bool parse_search_title(const char *json, char *title_out, size_t title_sz)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    bool ok = false;
    const cJSON *query = cJSON_GetObjectItemCaseSensitive(root, "query");
    if (query) {
        const cJSON *search = cJSON_GetObjectItemCaseSensitive(query, "search");
        if (cJSON_IsArray(search)) {
            const cJSON *first = cJSON_GetArrayItem(search, 0);
            if (first) {
                const cJSON *title = cJSON_GetObjectItemCaseSensitive(first, "title");
                if (cJSON_IsString(title) && title->valuestring) {
                    strncpy(title_out, title->valuestring, title_sz - 1);
                    title_out[title_sz - 1] = '\0';
                    ok = true;
                }
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool wiki_fetch_summary(const char *topic, wiki_article_t *out)
{
    char enc[256], url[360];
    url_encode(topic, enc, sizeof(enc));
    snprintf(url, sizeof(url),
             "https://%s.wikipedia.org/api/rest_v1/page/summary/%s", WIKI_LANG, enc);
    ESP_LOGI(TAG, "GET %s", url);
    char *body = http_get(url);
    if (!body) return false;
    bool ok = parse_summary_json(body, out);
    free(body);
    return ok;
}

bool wiki_fetch_random(wiki_article_t *out)
{
    char url[160];
    snprintf(url, sizeof(url),
             "https://%s.wikipedia.org/api/rest_v1/page/random/summary", WIKI_LANG);
    char *body = http_get(url);
    if (!body) return false;
    bool ok = parse_summary_json(body, out);
    free(body);
    return ok;
}

int wiki_count_images(const char *title)
{
    if (!title || !title[0]) return 0;

    char enc[256], url[360];
    url_encode(title, enc, sizeof(enc));
    snprintf(url, sizeof(url),
             "https://%s.wikipedia.org/api/rest_v1/page/media-list/%s", WIKI_LANG, enc);

    const int cap = 48 * 1024;                 // media-list can be large
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) return 0;
    buf[0] = '\0';
    resp_acc_t acc = { .buf = buf, .len = 0, .cap = cap };

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_evt,
        .user_data = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 12000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", WIKI_USER_AGENT);

    int count = 0;
    if (esp_http_client_perform(client) == ESP_OK &&
        esp_http_client_get_status_code(client) == 200) {
        const char *p = acc.buf;
        const char *needle = "\"type\":\"image\"";
        while ((p = strstr(p, needle)) != NULL) { count++; p += 14; }
    }
    esp_http_client_cleanup(client);
    free(buf);
    ESP_LOGI(TAG, "media-list: %d images for \"%s\"", count, title);
    return count;
}

int wiki_media_images(const char *title, char urls[][WIKI_IMG_URL_LEN], int max)
{
    if (!title || !title[0]) return 0;

    char enc[256], url[360];
    url_encode(title, enc, sizeof(enc));
    snprintf(url, sizeof(url),
             "https://%s.wikipedia.org/api/rest_v1/page/media-list/%s", WIKI_LANG, enc);

    const int cap = 64 * 1024;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) return 0;
    buf[0] = '\0';
    resp_acc_t acc = { .buf = buf, .len = 0, .cap = cap };

    esp_http_client_config_t cfg = {
        .url = url, .event_handler = http_evt, .user_data = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 12000, .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", WIKI_USER_AGENT);

    int total = 0;
    if (esp_http_client_perform(client) == ESP_OK &&
        esp_http_client_get_status_code(client) == 200) {
        cJSON *root = cJSON_Parse(acc.buf);
        if (root) {
            const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
            const cJSON *it = NULL;
            cJSON_ArrayForEach(it, items) {
                const cJSON *type = cJSON_GetObjectItemCaseSensitive(it, "type");
                if (!cJSON_IsString(type) || strcmp(type->valuestring, "image") != 0) continue;

                int idx = total++;
                if (idx >= max) continue;

                urls[idx][0] = '\0';
                const cJSON *srcset = cJSON_GetObjectItemCaseSensitive(it, "srcset");
                const cJSON *first  = cJSON_IsArray(srcset) ? cJSON_GetArrayItem(srcset, 0) : NULL;
                const cJSON *src    = first ? cJSON_GetObjectItemCaseSensitive(first, "src") : NULL;
                if (cJSON_IsString(src) && src->valuestring) {
                    const char *s = src->valuestring;      // often protocol-relative "//..."
                    if (s[0] == '/' && s[1] == '/') snprintf(urls[idx], WIKI_IMG_URL_LEN, "https:%s", s);
                    else                            snprintf(urls[idx], WIKI_IMG_URL_LEN, "%s", s);
                }
            }
            cJSON_Delete(root);
        }
    }
    esp_http_client_cleanup(client);
    free(buf);
    ESP_LOGI(TAG, "media-list: %d images for \"%s\"", total, title);
    return total;
}

bool wiki_search(const char *query, wiki_article_t *out)
{
    if (!query || !query[0]) return false;

    char enc[256], url[420];
    url_encode(query, enc, sizeof(enc));
    // Fuzzy full-text search; tolerates misspellings from noisy spelling.
    snprintf(url, sizeof(url),
             "https://%s.wikipedia.org/w/api.php?action=query&list=search"
             "&srsearch=%s&srlimit=1&format=json&utf8=1", WIKI_LANG, enc);
    ESP_LOGI(TAG, "SEARCH %s", url);

    char *body = http_get(url);
    if (!body) return false;

    char title[128];
    bool got = parse_search_title(body, title, sizeof(title));
    free(body);
    if (!got) {
        ESP_LOGW(TAG, "no search result for \"%s\"", query);
        return false;
    }
    ESP_LOGI(TAG, "search \"%s\" -> \"%s\"", query, title);
    return wiki_fetch_summary(title, out);   // get the clean summary for that title
}
