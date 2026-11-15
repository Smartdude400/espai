#include "display_ui.h"
#include "app_config.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "ui";

static lv_obj_t *s_title_lbl;
static lv_obj_t *s_body_lbl;
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_body_cont;   // scrollable container holding the body
static bool s_article_visible;  // true only while an article is on screen

// Gallery: images stacked below the text, appended as the user scrolls down.
#define UI_MAX_IMGS 16
static lv_obj_t      *s_imgs[UI_MAX_IMGS];
static lv_image_dsc_t *s_dscs[UI_MAX_IMGS];   // one dsc per image (kept alive)
static int            s_img_n;

static void backlight_on(void)
{
    gpio_config_t bk = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
    };
    gpio_config(&bk);
    gpio_set_level(LCD_PIN_BL, 1);
}

static void build_widgets(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // --- Title (top) ---
    s_title_lbl = lv_label_create(scr);
    lv_obj_set_width(s_title_lbl, LCD_H_RES - 8);
    lv_label_set_long_mode(s_title_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_title_lbl, lv_color_hex(0x33CCFF), 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_label_set_text(s_title_lbl, "");

    // --- Scrollable body container ---
    s_body_cont = lv_obj_create(scr);
    lv_obj_set_size(s_body_cont, LCD_H_RES, LCD_V_RES - 36);
    lv_obj_align(s_body_cont, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_set_style_bg_color(s_body_cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_body_cont, 0, 0);
    lv_obj_set_style_pad_all(s_body_cont, 4, 0);
    lv_obj_set_scroll_dir(s_body_cont, LV_DIR_VER);
    // Stack the text label and (optional) image vertically; centre the image.
    lv_obj_set_flex_flow(s_body_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body_cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body_cont, 8, 0);

    s_body_lbl = lv_label_create(s_body_cont);
    lv_obj_set_width(s_body_lbl, LCD_H_RES - 12);
    lv_label_set_long_mode(s_body_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_body_lbl, lv_color_white(), 0);
    lv_label_set_text(s_body_lbl, "");
    // Gallery images are appended after this label by ui_add_image_*().

    // --- Full-screen status label (shown for boot/status messages) ---
    s_status_lbl = lv_label_create(scr);
    lv_obj_set_width(s_status_lbl, LCD_H_RES - 16);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_status_lbl, lv_color_white(), 0);
    lv_obj_center(s_status_lbl);
    lv_label_set_text(s_status_lbl, "Booting...");
}

static void show_status_mode(bool status)
{
    s_article_visible = !status;
    if (status) {
        lv_obj_clear_flag(s_status_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_title_lbl,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_body_cont,  LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_status_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_title_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_body_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_init(void)
{
    backlight_on();

    // 1. SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Panel IO (4-wire SPI)
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

    // 3. ST7789 panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); // IPS panel
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 4. LVGL port
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * 40,
        .double_buffer = false,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,
        },
    };
    lvgl_port_add_disp(&disp_cfg);

    if (lvgl_port_lock(0)) {
        build_widgets();
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "display ready");
}

void ui_show_status(const char *msg)
{
    if (!lvgl_port_lock(100)) return;
    lv_label_set_text(s_status_lbl, msg);
    show_status_mode(true);
    lvgl_port_unlock();
}

void ui_show_article(const char *title, const char *body)
{
    if (!lvgl_port_lock(100)) return;
    lv_label_set_text(s_title_lbl, title);
    lv_label_set_text(s_body_lbl, body);
    lv_obj_scroll_to_y(s_body_cont, 0, LV_ANIM_OFF);
    show_status_mode(false);
    lvgl_port_unlock();
}

// Append a new image widget below the text, sizing its box to the scaled size
// so the flex layout never gets pushed wider than the screen. `d` is stored and
// kept alive; the pixel/encoded buffer is owned by the caller (the gallery).
static bool add_image(lv_image_dsc_t *d, int w, int h)
{
    if (s_img_n >= UI_MAX_IMGS) return false;
    if (!lvgl_port_lock(200)) return false;

    lv_obj_t *img = lv_image_create(s_body_cont);
    lv_image_set_src(img, d);

    int maxw = LCD_H_RES - 8;
    int scale = (w > maxw && w > 0) ? (maxw * 256 / w) : 256;
    lv_image_set_pivot(img, 0, 0);          // scale from top-left
    lv_image_set_scale(img, scale);
    if (w > 0 && h > 0) lv_obj_set_size(img, w * scale / 256, h * scale / 256);

    s_imgs[s_img_n] = img;
    s_dscs[s_img_n] = d;
    s_img_n++;
    lvgl_port_unlock();
    return true;
}

bool ui_add_image_rgb565(const uint8_t *rgb565, int w, int h)
{
    if (!rgb565 || w <= 0 || h <= 0 || s_img_n >= UI_MAX_IMGS) return false;
    lv_image_dsc_t *d = calloc(1, sizeof(*d));
    if (!d) return false;
    d->header.magic  = LV_IMAGE_HEADER_MAGIC;
    d->header.cf     = LV_COLOR_FORMAT_RGB565;
    d->header.w      = w;
    d->header.h      = h;
    d->header.stride = w * 2;
    d->data          = rgb565;
    d->data_size     = (uint32_t)w * h * 2;
    if (!add_image(d, w, h)) { free(d); return false; }
    return true;
}

bool ui_add_image_encoded(const uint8_t *data, uint32_t len, int w, int h)
{
    if (!data || len < 8 || s_img_n >= UI_MAX_IMGS) return false;
    lv_image_dsc_t *d = calloc(1, sizeof(*d));
    if (!d) return false;
    d->header.magic  = LV_IMAGE_HEADER_MAGIC;
    d->header.cf     = LV_COLOR_FORMAT_RAW;   // encoded; a decoder handles it
    d->header.w      = 0;                     // filled in by the PNG decoder
    d->header.h      = 0;
    d->header.stride = 0;
    d->data          = data;
    d->data_size     = len;
    if (!add_image(d, w, h)) { free(d); return false; }
    return true;
}

void ui_clear_images(void)
{
    if (!lvgl_port_lock(200)) return;
    for (int i = 0; i < s_img_n; i++) {
        if (s_imgs[i]) lv_obj_delete(s_imgs[i]);
        if (s_dscs[i]) free(s_dscs[i]);
        s_imgs[i] = NULL;
        s_dscs[i] = NULL;
    }
    s_img_n = 0;
    lvgl_port_unlock();
}

int32_t ui_scroll_bottom(void)
{
    if (!s_article_visible) return 1000000;
    if (!lvgl_port_lock(50)) return 1000000;
    int32_t r = lv_obj_get_scroll_bottom(s_body_cont);
    lvgl_port_unlock();
    return r;
}

void ui_show_spelling(const char *word)
{
    char up[80];
    size_t i = 0;
    for (; word[i] && i < sizeof(up) - 1; i++) {
        char c = word[i];
        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    up[i] = '\0';

    char buf[160];
    snprintf(buf, sizeof(buf),
             "Spell with NATO\n(say \"search\" to look up)\n\n%s_", up);

    if (!lvgl_port_lock(100)) return;
    lv_label_set_text(s_status_lbl, buf);
    show_status_mode(true);
    lvgl_port_unlock();
}

void ui_scroll(int dir)
{
    if (!s_article_visible) return;
    if (!lvgl_port_lock(100)) return;
    lv_obj_scroll_by(s_body_cont, 0, dir > 0 ? -120 : 120, LV_ANIM_ON);
    lvgl_port_unlock();
}

bool ui_article_visible(void)
{
    return s_article_visible;
}

void ui_scroll_px(int dy)
{
    if (!s_article_visible || dy == 0) return;
    if (!lvgl_port_lock(20)) return;      // short wait; skip this tick if busy
    // +dy = further down the page (content moves up).
    lv_obj_scroll_by(s_body_cont, 0, -dy, LV_ANIM_OFF);
    lvgl_port_unlock();
}
