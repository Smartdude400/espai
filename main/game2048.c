#include "game2048.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "2048";

#define SCR   240
#define N_MAX 6
#define GAP   3

static int s_b[N_MAX][N_MAX];
static int s_n, s_score, s_tile;
static lv_obj_t *s_ov, *s_tiles[N_MAX][N_MAX], *s_lbls[N_MAX][N_MAX];
static lv_obj_t *s_score_lbl, *s_over_lbl;
static bool s_dead, s_armed;
static volatile bool s_on;

static uint32_t tile_bg(int v)
{
    switch (v) {
        case 0:    return 0xCDC1B4;
        case 2:    return 0xEEE4DA;
        case 4:    return 0xEDE0C8;
        case 8:    return 0xF2B179;
        case 16:   return 0xF59563;
        case 32:   return 0xF67C5F;
        case 64:   return 0xF65E3B;
        case 128:  return 0xEDCF72;
        case 256:  return 0xEDCC61;
        case 512:  return 0xEDC850;
        case 1024: return 0xEDC53F;
        case 2048: return 0xEDC22E;
        default:   return 0x3C3A32;
    }
}

// Board accessors remapped so every direction slides "toward index 0".
static int  bget(int dir, int k, int i)
{
    switch (dir) {
        case 0:  return s_b[i][k];               // up
        case 1:  return s_b[s_n - 1 - i][k];     // down
        case 2:  return s_b[k][i];               // left
        default: return s_b[k][s_n - 1 - i];     // right
    }
}
static void bset(int dir, int k, int i, int v)
{
    switch (dir) {
        case 0:  s_b[i][k] = v; break;
        case 1:  s_b[s_n - 1 - i][k] = v; break;
        case 2:  s_b[k][i] = v; break;
        default: s_b[k][s_n - 1 - i] = v; break;
    }
}

static void spawn_tile(void)
{
    int empties = 0;
    for (int r = 0; r < s_n; r++)
        for (int c = 0; c < s_n; c++)
            if (!s_b[r][c]) empties++;
    if (!empties) return;
    int pick = esp_random() % empties;
    for (int r = 0; r < s_n; r++) {
        for (int c = 0; c < s_n; c++) {
            if (!s_b[r][c] && pick-- == 0) {
                s_b[r][c] = (esp_random() % 10 == 0) ? 4 : 2;
                return;
            }
        }
    }
}

static bool board_dead(void)
{
    for (int r = 0; r < s_n; r++)
        for (int c = 0; c < s_n; c++) {
            if (!s_b[r][c]) return false;
            if (r + 1 < s_n && s_b[r][c] == s_b[r + 1][c]) return false;
            if (c + 1 < s_n && s_b[r][c] == s_b[r][c + 1]) return false;
        }
    return true;
}

// Must be called with the LVGL lock held.
static void render(void)
{
    for (int r = 0; r < s_n; r++) {
        for (int c = 0; c < s_n; c++) {
            int v = s_b[r][c];
            lv_obj_set_style_bg_color(s_tiles[r][c], lv_color_hex(tile_bg(v)), 0);
            if (v) {
                char t[8];
                snprintf(t, sizeof(t), "%d", v);
                lv_label_set_text(s_lbls[r][c], t);
                lv_obj_set_style_text_color(s_lbls[r][c],
                    lv_color_hex(v <= 4 ? 0x776E65 : 0xF9F6F2), 0);
            } else {
                lv_label_set_text(s_lbls[r][c], "");
            }
        }
    }
    char sc[32];
    snprintf(sc, sizeof(sc), "Score: %d", s_score);
    lv_label_set_text(s_score_lbl, sc);

    if (s_dead) lv_obj_clear_flag(s_over_lbl, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(s_over_lbl, LV_OBJ_FLAG_HIDDEN);
}

static bool slide(int dir)
{
    bool changed = false;
    for (int k = 0; k < s_n; k++) {
        int line[N_MAX], m = 0;
        for (int i = 0; i < s_n; i++) {
            int v = bget(dir, k, i);
            if (v) line[m++] = v;
        }
        int out[N_MAX], o = 0;
        for (int i = 0; i < m; i++) {
            if (i + 1 < m && line[i] == line[i + 1]) {
                out[o] = line[i] * 2;
                s_score += out[o];
                if (out[o] > s_tile) s_tile = out[o];
                o++; i++;
            } else {
                out[o++] = line[i];
            }
        }
        while (o < s_n) out[o++] = 0;
        for (int i = 0; i < s_n; i++) {
            if (bget(dir, k, i) != out[i]) changed = true;
            bset(dir, k, i, out[i]);
        }
    }
    return changed;
}

bool g2048_active(void) { return s_on; }

void g2048_restart(void)
{
    memset(s_b, 0, sizeof(s_b));
    s_score = 0;
    s_tile = 0;
    s_dead = false;
    spawn_tile();
    spawn_tile();
    if (lvgl_port_lock(200)) { render(); lvgl_port_unlock(); }
}

void g2048_move(int dir)
{
    if (!s_on || dir < 0 || dir > 3) return;
    if (s_dead) return;
    if (!lvgl_port_lock(100)) return;
    if (slide(dir)) {
        spawn_tile();
        s_dead = board_dead();
    }
    render();
    lvgl_port_unlock();
}

void g2048_tilt(float ax, float ay)
{
    if (!s_on) return;
    float gx = GAME_INV_X ? -ax : ax;
    float gy = GAME_INV_Y ? -ay : ay;
    float m = fmaxf(fabsf(gx), fabsf(gy));

    if (m < 0.12f) { s_armed = true; return; }   // board level again -> re-arm
    if (!s_armed || m < 0.35f) return;           // one move per tilt gesture
    s_armed = false;

    if (s_dead) { g2048_restart(); return; }
    int dir = (fabsf(gx) > fabsf(gy)) ? (gx > 0 ? 3 : 2) : (gy > 0 ? 1 : 0);
    g2048_move(dir);
}

void g2048_start(int n)
{
    if (s_on) g2048_stop();
    if (n < 3) n = 3;
    if (n > N_MAX) n = N_MAX;
    s_n = n;

    if (!lvgl_port_lock(500)) return;

    s_ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ov, SCR, SCR);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_set_style_pad_all(s_ov, 0, 0);
    lv_obj_set_style_border_width(s_ov, 0, 0);
    lv_obj_set_style_radius(s_ov, 0, 0);
    lv_obj_set_style_bg_color(s_ov, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);

    s_score_lbl = lv_label_create(s_ov);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(0x776E65), 0);
    lv_obj_set_pos(s_score_lbl, 6, 5);
    lv_label_set_text(s_score_lbl, "Score: 0");

    // Board layout: fills the area under the score line.
    int top = 24;
    int avail = SCR - top - 4;
    int tile = (avail - (s_n + 1) * GAP) / s_n;
    int bx = (SCR - (s_n * tile + (s_n + 1) * GAP)) / 2;

    for (int r = 0; r < s_n; r++) {
        for (int c = 0; c < s_n; c++) {
            lv_obj_t *t = lv_obj_create(s_ov);
            lv_obj_set_size(t, tile, tile);
            lv_obj_set_pos(t, bx + GAP + c * (tile + GAP), top + GAP + r * (tile + GAP));
            lv_obj_set_style_radius(t, 3, 0);
            lv_obj_set_style_border_width(t, 0, 0);
            lv_obj_set_style_pad_all(t, 0, 0);
            lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *l = lv_label_create(t);
            lv_obj_center(l);
            s_tiles[r][c] = t;
            s_lbls[r][c]  = l;
        }
    }

    s_over_lbl = lv_label_create(s_ov);
    lv_label_set_text(s_over_lbl, "GAME OVER\ntilt to restart");
    lv_obj_set_style_text_align(s_over_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_over_lbl, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_over_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_over_lbl, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_over_lbl, 8, 0);
    lv_obj_center(s_over_lbl);
    lv_obj_add_flag(s_over_lbl, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();

    s_on = true;
    s_armed = false;
    g2048_restart();
    ESP_LOGI(TAG, "2048 %dx%d started (tile %dpx)", n, n, tile);
}

void g2048_stop(void)
{
    if (!s_on && !s_ov) return;
    s_on = false;
    if (lvgl_port_lock(500)) {
        if (s_ov) { lv_obj_delete(s_ov); s_ov = NULL; }
        lvgl_port_unlock();
    }
}
