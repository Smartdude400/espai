#include "wiki_image.h"
#include "display_ui.h"
#include "wikipedia.h"
#include "app_config.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_common.h"

static const char *TAG = "wimg";

#define GAL_MAX       12            // how many images we'll load at most
#define IMG_DL_MAX    (300 * 1024)  // max compressed image we'll accept
#define WIKI_USER_AGENT "espai-esp32/1.0 (https://github.com/; hobby project)"

// ---- gallery state (touched ONLY by the gallery task) --------------------
static char      s_urls[GAL_MAX][WIKI_IMG_URL_LEN];
static int       s_have;            // urls available (<= GAL_MAX)
static int       s_loaded;          // urls attempted so far
static bool      s_active;
static uint8_t  *s_bufs[GAL_MAX];   // kept-alive image buffers
static bool      s_aligned[GAL_MAX];// true -> jpeg_free_align, false -> free
static int       s_bufn;

static QueueHandle_t s_q;           // length-1 queue of pending article titles

// ---- download ------------------------------------------------------------
typedef struct { uint8_t *buf; int len; int cap; } dl_t;

static esp_err_t dl_evt(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        dl_t *d = (dl_t *)evt->user_data;
        if (d && d->buf && d->len + evt->data_len <= d->cap) {
            memcpy(d->buf + d->len, evt->data, evt->data_len);
            d->len += evt->data_len;
        }
    }
    return ESP_OK;
}

static uint8_t *download(const char *url, int *out_len)
{
    dl_t d = { .buf = heap_caps_malloc(IMG_DL_MAX, MALLOC_CAP_SPIRAM), .len = 0, .cap = IMG_DL_MAX };
    if (!d.buf) { ESP_LOGE(TAG, "no PSRAM for download"); return NULL; }

    esp_http_client_config_t cfg = {
        .url = url, .event_handler = dl_evt, .user_data = &d,
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 15000, .buffer_size = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "User-Agent", WIKI_USER_AGENT);

    uint8_t *result = NULL;
    if (esp_http_client_perform(c) == ESP_OK) {
        int status = esp_http_client_get_status_code(c);
        if (status == 200 && d.len > 0) { *out_len = d.len; result = d.buf; d.buf = NULL; }
        else ESP_LOGW(TAG, "image HTTP %d", status);
    } else {
        ESP_LOGW(TAG, "image download failed");
    }
    esp_http_client_cleanup(c);
    if (d.buf) free(d.buf);
    return result;
}

// Decode a JPEG into an aligned RGB565 buffer (caller frees with jpeg_free_align).
static uint8_t *decode_jpeg(uint8_t *jpg, int jlen, int *w, int *h)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return NULL;

    jpeg_dec_io_t io = { .inbuf = jpg, .inbuf_len = jlen };
    jpeg_dec_header_info_t info = { 0 };
    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK) { jpeg_dec_close(dec); return NULL; }

    int outlen = 0;
    jpeg_dec_get_outbuf_len(dec, &outlen);
    uint8_t *out = jpeg_calloc_align(outlen, 16);
    if (!out) { jpeg_dec_close(dec); return NULL; }

    io.outbuf = out;
    jpeg_error_t r = jpeg_dec_process(dec, &io);
    jpeg_dec_close(dec);
    if (r != JPEG_ERR_OK) { jpeg_free_align(out); return NULL; }

    *w = info.width;
    *h = info.height;
    return out;
}

// Download + decode the next image URL and append it under the text.
static void load_next(void)
{
    if (s_loaded >= s_have || s_bufn >= GAL_MAX) return;
    const char *url = s_urls[s_loaded++];
    if (!url[0]) return;

    ESP_LOGI(TAG, "GET %s", url);
    int len = 0;
    uint8_t *img = download(url, &len);
    if (!img) return;

    if (len > 3 && img[0] == 0xFF && img[1] == 0xD8) {             // JPEG
        int w = 0, h = 0;
        uint8_t *rgb = decode_jpeg(img, len, &w, &h);
        free(img);
        if (rgb && ui_add_image_rgb565(rgb, w, h)) {
            s_bufs[s_bufn] = rgb; s_aligned[s_bufn] = true; s_bufn++;
            ESP_LOGI(TAG, "JPEG %dx%d (#%d)", w, h, s_bufn);
        } else if (rgb) {
            jpeg_free_align(rgb);
        }
    } else if (len > 8 && img[0] == 0x89 && img[1] == 0x50 &&
               img[2] == 0x4E && img[3] == 0x47) {                 // PNG
        int w = (img[16] << 24) | (img[17] << 16) | (img[18] << 8) | img[19];
        int h = (img[20] << 24) | (img[21] << 16) | (img[22] << 8) | img[23];
        if (ui_add_image_encoded(img, (uint32_t)len, w, h)) {
            s_bufs[s_bufn] = img; s_aligned[s_bufn] = false; s_bufn++;
            ESP_LOGI(TAG, "PNG %dx%d (#%d)", w, h, s_bufn);
        } else {
            free(img);
        }
    } else {
        ESP_LOGI(TAG, "unsupported image format, skipping");
        free(img);
    }
}

static void gallery_clear(void)
{
    s_active = false;
    ui_clear_images();                         // delete widgets first...
    for (int i = 0; i < s_bufn; i++) {         // ...then free their buffers
        if (s_aligned[i]) jpeg_free_align(s_bufs[i]);
        else free(s_bufs[i]);
        s_bufs[i] = NULL;
    }
    s_bufn = 0;
    s_loaded = 0;
    s_have = 0;
}

static void gallery_task(void *arg)
{
    char title[128];
    while (1) {
        if (xQueueReceive(s_q, title, pdMS_TO_TICKS(200)) == pdTRUE) {
            // New article requested.
            gallery_clear();
            int total = wiki_media_images(title, s_urls, GAL_MAX);
            s_have = (total < GAL_MAX) ? total : GAL_MAX;
            s_active = (s_have > 0);
            if (s_active) load_next();          // show the first image right away
        } else if (s_active && s_loaded < s_have) {
            // Lazily load more as the user nears the bottom.
            if (ui_scroll_bottom() < LCD_V_RES) load_next();
        }
    }
}

void gallery_start(void)
{
    s_q = xQueueCreate(1, sizeof(char[128]));
    xTaskCreate(gallery_task, "gallery", 16384, NULL, 4, NULL);
}

void gallery_request(const char *title)
{
    if (!s_q || !title) return;
    char buf[128];
    strncpy(buf, title, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    xQueueOverwrite(s_q, buf);   // only the latest article matters
}
