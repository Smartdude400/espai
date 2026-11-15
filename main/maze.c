#include "maze.h"
#include "app_config.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "maze";

#define SCR      240
#define WALL_T   3            // wall thickness in px
#define MAZE_MAX 14

// Wall bits per cell.
#define W_N 1
#define W_E 2
#define W_S 4
#define W_W 8

static uint16_t *s_fb;                    // 240x240 RGB565 framebuffer (PSRAM)
static lv_image_dsc_t s_dsc;
static lv_obj_t *s_ov, *s_img, *s_ball, *s_msg;
static uint8_t s_walls[MAZE_MAX][MAZE_MAX];
static int   s_n, s_cell, s_ox, s_oy;     // grid size, cell px, origin offset
static float s_x, s_y, s_vx, s_vy, s_r;   // ball state (screen px)
static int   s_wintick;                   // >0 while the WIN banner shows
static volatile bool s_on;

static void fb_rect(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR) w = SCR - x;
    if (y + h > SCR) h = SCR - y;
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            s_fb[j * SCR + i] = c;
}

// Depth-first "recursive backtracker" with an explicit stack.
static void gen_maze(void)
{
    memset(s_walls, W_N | W_E | W_S | W_W, sizeof(s_walls));
    static bool vis[MAZE_MAX][MAZE_MAX];
    memset(vis, 0, sizeof(vis));

    static uint8_t stx[MAZE_MAX * MAZE_MAX], sty[MAZE_MAX * MAZE_MAX];
    int sp = 0, cx = 0, cy = 0;
    vis[0][0] = true;

    const int dx[4]  = { 0, 1, 0, -1 };
    const int dy[4]  = { -1, 0, 1, 0 };
    const uint8_t bit[4] = { W_N, W_E, W_S, W_W };
    const uint8_t opp[4] = { W_S, W_W, W_N, W_E };

    while (1) {
        int cand[4], nd = 0;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx >= 0 && nx < s_n && ny >= 0 && ny < s_n && !vis[ny][nx]) cand[nd++] = d;
        }
        if (nd) {
            int d = cand[esp_random() % nd];
            int nx = cx + dx[d], ny = cy + dy[d];
            s_walls[cy][cx] &= ~bit[d];
            s_walls[ny][nx] &= ~opp[d];
            stx[sp] = cx; sty[sp] = cy; sp++;
            cx = nx; cy = ny;
            vis[cy][cx] = true;
        } else if (sp > 0) {
            sp--; cx = stx[sp]; cy = sty[sp];
        } else break;
    }
}

static void draw_maze(void)
{
    for (int i = 0; i < SCR * SCR; i++) s_fb[i] = 0x0841;   // near-black bg

    int span = s_n * s_cell;

    // Goal cell (bottom-right) in green.
    fb_rect(s_ox + (s_n - 1) * s_cell + WALL_T, s_oy + (s_n - 1) * s_cell + WALL_T,
            s_cell - WALL_T, s_cell - WALL_T, 0x0640);

    // Each cell draws its N and W walls; outer S/E drawn after.
    for (int r = 0; r < s_n; r++) {
        for (int c = 0; c < s_n; c++) {
            if (s_walls[r][c] & W_N)
                fb_rect(s_ox + c * s_cell, s_oy + r * s_cell, s_cell + WALL_T, WALL_T, 0xFFFF);
            if (s_walls[r][c] & W_W)
                fb_rect(s_ox + c * s_cell, s_oy + r * s_cell, WALL_T, s_cell + WALL_T, 0xFFFF);
        }
    }
    fb_rect(s_ox, s_oy + span, span + WALL_T, WALL_T, 0xFFFF);   // bottom
    fb_rect(s_ox + span, s_oy, WALL_T, span + WALL_T, 0xFFFF);   // right
}

static void ball_reset(void)
{
    s_x = s_ox + WALL_T + (s_cell - WALL_T) / 2.0f;
    s_y = s_oy + WALL_T + (s_cell - WALL_T) / 2.0f;
    s_vx = s_vy = 0;
}

bool maze_active(void) { return s_on; }

void maze_start(int n)
{
    if (s_on) maze_stop();
    if (n < 4) n = 4;
    if (n > MAZE_MAX) n = MAZE_MAX;
    s_n = n;
    s_cell = (SCR - 8 - WALL_T) / n;
    s_ox = (SCR - (n * s_cell + WALL_T)) / 2;
    s_oy = s_ox;
    s_r  = s_cell * 0.28f;
    if (s_r < 3) s_r = 3;

    s_fb = heap_caps_malloc(SCR * SCR * 2, MALLOC_CAP_SPIRAM);
    if (!s_fb) { ESP_LOGE(TAG, "no PSRAM for maze"); return; }

    gen_maze();
    draw_maze();
    ball_reset();
    s_wintick = 0;

    s_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w      = SCR;
    s_dsc.header.h      = SCR;
    s_dsc.header.stride = SCR * 2;
    s_dsc.data          = (const uint8_t *)s_fb;
    s_dsc.data_size     = SCR * SCR * 2;

    if (!lvgl_port_lock(500)) { free(s_fb); s_fb = NULL; return; }

    s_ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ov, SCR, SCR);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_set_style_pad_all(s_ov, 0, 0);
    lv_obj_set_style_border_width(s_ov, 0, 0);
    lv_obj_set_style_radius(s_ov, 0, 0);
    lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);

    s_img = lv_image_create(s_ov);
    lv_image_set_src(s_img, &s_dsc);
    lv_obj_set_pos(s_img, 0, 0);

    s_ball = lv_obj_create(s_ov);
    lv_obj_set_size(s_ball, (int)(s_r * 2), (int)(s_r * 2));
    lv_obj_set_style_radius(s_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ball, lv_color_hex(0xFFB000), 0);
    lv_obj_set_style_border_width(s_ball, 0, 0);
    lv_obj_clear_flag(s_ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_ball, (int)(s_x - s_r), (int)(s_y - s_r));

    s_msg = lv_label_create(s_ov);
    lv_label_set_text(s_msg, "YOU WIN!");
    lv_obj_set_style_text_color(s_msg, lv_color_hex(0x33FF66), 0);
    lv_obj_set_style_bg_color(s_msg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_msg, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_msg, 8, 0);
    lv_obj_center(s_msg);
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();
    s_on = true;
    ESP_LOGI(TAG, "maze %dx%d started (cell %dpx)", n, n, s_cell);
}

void maze_stop(void)
{
    if (!s_on && !s_fb) return;
    s_on = false;
    if (lvgl_port_lock(500)) {
        if (s_ov) { lv_obj_delete(s_ov); s_ov = NULL; }
        lvgl_port_unlock();
    }
    if (s_fb) { free(s_fb); s_fb = NULL; }
}

void maze_tilt(float ax, float ay)
{
    if (!s_on) return;

    const float dt = IMU_POLL_MS / 1000.0f;
    float gx = GAME_INV_X ? -ax : ax;
    float gy = GAME_INV_Y ? -ay : ay;

    if (!lvgl_port_lock(15)) return;      // skip this tick if LVGL is busy
    if (!s_on) { lvgl_port_unlock(); return; }

    if (s_wintick > 0) {                  // WIN banner showing
        if (--s_wintick == 0) {
            gen_maze();
            draw_maze();
            ball_reset();
            lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(s_img);
        }
        lvgl_port_unlock();
        return;
    }

    // Integrate velocity (px/s) with a light rolling friction.
    s_vx += gx * 2600.0f * dt;
    s_vy += gy * 2600.0f * dt;
    s_vx *= 0.965f;
    s_vy *= 0.965f;
    const float vmax = 320.0f;
    if (s_vx >  vmax) s_vx =  vmax;
    if (s_vx < -vmax) s_vx = -vmax;
    if (s_vy >  vmax) s_vy =  vmax;
    if (s_vy < -vmax) s_vy = -vmax;

    int span = s_n * s_cell;

    // --- X step, collide against the current cell's E/W walls ---
    int col = (int)((s_x - s_ox) / s_cell);
    int row = (int)((s_y - s_oy) / s_cell);
    if (col < 0) col = 0;
    if (col >= s_n) col = s_n - 1;
    if (row < 0) row = 0;
    if (row >= s_n) row = s_n - 1;

    float nx = s_x + s_vx * dt;
    if (s_vx > 0 && (s_walls[row][col] & W_E)) {
        float lim = s_ox + (col + 1) * s_cell - s_r;
        if (nx > lim) { nx = lim; s_vx *= -0.35f; }
    } else if (s_vx < 0 && (s_walls[row][col] & W_W)) {
        float lim = s_ox + col * s_cell + WALL_T + s_r;
        if (nx < lim) { nx = lim; s_vx *= -0.35f; }
    }
    if (nx < s_ox + s_r) { nx = s_ox + s_r; s_vx *= -0.35f; }
    if (nx > s_ox + span - s_r) { nx = s_ox + span - s_r; s_vx *= -0.35f; }
    s_x = nx;

    // --- Y step (recompute the cell; X may have crossed a boundary) ---
    col = (int)((s_x - s_ox) / s_cell);
    if (col < 0) col = 0;
    if (col >= s_n) col = s_n - 1;

    float ny = s_y + s_vy * dt;
    if (s_vy > 0 && (s_walls[row][col] & W_S)) {
        float lim = s_oy + (row + 1) * s_cell - s_r;
        if (ny > lim) { ny = lim; s_vy *= -0.35f; }
    } else if (s_vy < 0 && (s_walls[row][col] & W_N)) {
        float lim = s_oy + row * s_cell + WALL_T + s_r;
        if (ny < lim) { ny = lim; s_vy *= -0.35f; }
    }
    if (ny < s_oy + s_r) { ny = s_oy + s_r; s_vy *= -0.35f; }
    if (ny > s_oy + span - s_r) { ny = s_oy + span - s_r; s_vy *= -0.35f; }
    s_y = ny;

    lv_obj_set_pos(s_ball, (int)(s_x - s_r), (int)(s_y - s_r));

    // --- Win check: ball inside the goal (bottom-right) cell ---
    col = (int)((s_x - s_ox) / s_cell);
    row = (int)((s_y - s_oy) / s_cell);
    if (col == s_n - 1 && row == s_n - 1) {
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        s_wintick = 45;                   // ~1.4s, then a fresh maze
    }

    lvgl_port_unlock();
}
