#pragma once
#include <stdbool.h>
#include <stdint.h>

// Bring up the ST7789V2 LCD + LVGL. Call once at boot.
void ui_init(void);

// Show a single centered status line (e.g. "Connecting Wi-Fi...").
void ui_show_status(const char *msg);

// Show an article: bold-ish title at top, wrapped body below.
void ui_show_article(const char *title, const char *body);

// Show the spelling-in-progress screen (the word being built letter by letter).
void ui_show_spelling(const char *word);

// Scroll the article body by one "page" ( +1 down, -1 up ).
void ui_scroll(int dir);

// Scroll the article body by a pixel amount ( +dy = further down the page ).
// Used by tilt-to-scroll. No-op unless an article is currently shown.
void ui_scroll_px(int dy);

// True while an article is on screen (vs a status / spelling / debug screen).
bool ui_article_visible(void);

// Gallery: append images below the text (they stack and scroll with it).
// The caller owns the pixel/encoded buffer and must keep it valid until
// ui_clear_images() is called. Return false if it couldn't be added.
bool ui_add_image_rgb565(const uint8_t *rgb565, int w, int h);              // decoded RGB565
bool ui_add_image_encoded(const uint8_t *data, uint32_t len, int w, int h); // e.g. PNG bytes

// Remove all gallery images (call before loading a new article's images).
void ui_clear_images(void);

// Pixels still scrollable below the current view (0 = at the bottom). Used to
// decide when to lazily load the next image.
int32_t ui_scroll_bottom(void);
