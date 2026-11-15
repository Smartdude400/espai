// ===========================================================================
//  espai - on-device Wikipedia gadget for Waveshare ESP32-S3-LCD-1.3
//
//  "Spell any word" mode, fully standalone:
//    Hold the button, speak ONE word per press:
//      NATO letters (alpha..zulu) -> spell the topic
//      "delete" / "space" / "clear" -> edit
//      "search" -> look it up on Wikipedia (fuzzy, so typos are OK)
//      "random article" / "go back" -> shortcuts
//    Speech is recognised ON THE CHIP (ESP-SR MultiNet). Only the article text
//    comes from Wikipedia over Wi-Fi.
//
//  NO-MIC TEST MODE (app_config.h -> TEST_NO_MIC 1):
//    Skips the microphone and lets you TYPE the same commands into the
//    `idf.py monitor` terminal, so you can test Wi-Fi + the display + the whole
//    search/render pipeline before the mic arrives.
// ===========================================================================
#include "app_config.h"
#include "wifi_conn.h"
#include "wikipedia.h"
#include "display_ui.h"
#include "audio_mic.h"
#include "speech_rec.h"
#include "imu.h"
#include "wiki_image.h"
#include "maze.h"
#include "game2048.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#if TEST_NO_MIC
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

static const char *TAG = "app";

static char s_spell[64];
static int  s_spell_len;

static wiki_article_t s_cur, s_prev;
static wiki_article_t s_fetch;          // scratch buffer (keeps big frames off the stack)
static bool s_have_cur, s_have_prev;
static bool s_speech_ok;

static void button_init(void)
{
    gpio_config_t io = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io);
}

static bool button_pressed(void)
{
    int level = gpio_get_level(BUTTON_PIN);
    return BUTTON_ACTIVE_LOW ? (level == 0) : (level == 1);
}

// Leave whichever game is running and restore the article / spelling screen.
static void exit_games(void)
{
    maze_stop();
    g2048_stop();
    if (s_have_cur) ui_show_article(s_cur.title, s_cur.extract);
    else            ui_show_spelling(s_spell);
}

static void show_article(const wiki_article_t *a)
{
    if (s_have_cur) { s_prev = s_cur; s_have_prev = true; }
    s_cur = *a;
    s_have_cur = true;

    int photos = wiki_count_images(a->title);
    static char body[2200];
    if (photos > 0)
        snprintf(body, sizeof(body), "(%d photo%s below)\n\n%s",
                 photos, photos == 1 ? "" : "s", a->extract);
    else
        snprintf(body, sizeof(body), "%s", a->extract);

    ui_show_article(a->title, body);
    ESP_LOGI(TAG, "showing: %s (%d photos)", a->title, photos);
    gallery_request(a->title);   // gallery lazily loads images below the text
}

static void do_search(void)
{
    if (s_spell_len == 0) { ui_show_spelling(s_spell); return; }
    ESP_LOGI(TAG, "searching: \"%s\"", s_spell);
    ui_show_status("Searching\nWikipedia...");
    if (wiki_search(s_spell, &s_fetch)) show_article(&s_fetch);
    else ui_show_status("Nothing found.\nTry again.");
    s_spell_len = 0;
    s_spell[0] = '\0';
}

static void do_random(void)
{
    ui_show_status("Searching\nWikipedia...");
    if (wiki_fetch_random(&s_fetch)) show_article(&s_fetch);
    else ui_show_status("Lookup failed.\nTry again.");
}

static void handle_command(int cmd)
{
    char letter = speech_id_to_letter(cmd);
    if (letter) {
        if (s_spell_len < (int)sizeof(s_spell) - 1) {
            s_spell[s_spell_len++] = letter;
            s_spell[s_spell_len] = '\0';
        }
        ui_show_spelling(s_spell);
        return;
    }

    switch (cmd) {
        case CMD_SPACE:
            if (s_spell_len < (int)sizeof(s_spell) - 1) {
                s_spell[s_spell_len++] = ' ';
                s_spell[s_spell_len] = '\0';
            }
            ui_show_spelling(s_spell);
            break;
        case CMD_DELETE:
            if (s_spell_len > 0) s_spell[--s_spell_len] = '\0';
            ui_show_spelling(s_spell);
            break;
        case CMD_CLEAR:
            s_spell_len = 0; s_spell[0] = '\0';
            ui_show_spelling(s_spell);
            break;
        case CMD_SEARCH: do_search(); break;
        case CMD_RANDOM: do_random(); break;
        case CMD_MAZE:   g2048_stop(); maze_start(GAME_DEFAULT_MAZE_N);  break;
        case CMD_2048:   maze_stop();  g2048_start(GAME_DEFAULT_2048_N); break;
        case CMD_EXIT_GAME: exit_games(); break;
        case CMD_BACK:
            if (s_have_prev) {
                wiki_article_t t = s_cur; s_cur = s_prev; s_prev = t;
                ui_show_article(s_cur.title, s_cur.extract);
                gallery_request(s_cur.title);
            } else {
                ui_show_status("No previous\narticle.");
            }
            break;
        default:
            ui_show_spelling(s_spell);
            break;
    }
}

// ---------------------------------------------------------------------------
//  Tilt-to-scroll: read the IMU and scroll the article while the board is tilted
// ---------------------------------------------------------------------------
#if IMU_ENABLE
static bool s_imu_ok;

#if IMU_DEBUG_SCREEN
static void imu_show_readout(float ax, float ay, float az)
{
    static int throttle = 0;
    if ((++throttle % 5) != 0) return;    // ~6 Hz update
    float axisv = (IMU_TILT_AXIS == 0) ? ax : (IMU_TILT_AXIS == 1) ? ay : az;
    char buf[192];
    snprintf(buf, sizeof(buf),
             "TILT TEST\n\nx=%d  y=%d  z=%d\n\naxis %d = %d\n(needs > %d)\n\ntilt to test,\nor type =neptune",
             (int)(ax * 100), (int)(ay * 100), (int)(az * 100),
             IMU_TILT_AXIS, (int)(axisv * 100), (int)(IMU_TILT_DEADZONE * 100));
    ui_show_status(buf);
}
#endif

static void imu_scroll_task(void *arg)
{
    if (!s_imu_ok) {
        while (1) {
#if IMU_DEBUG_SCREEN
            if (!ui_article_visible())
                ui_show_status("IMU NOT FOUND\n\nQMI8658 not on\nI2C (SDA 47,\nSCL 48).");
#endif
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }

    float residual = 0.0f;      // sub-pixel accumulator for smooth scrolling
#if IMU_DEBUG_SCREEN
    int   dbg = 0;
#endif
    while (1) {
        float ax, ay, az;
        if (imu_read_accel_g(&ax, &ay, &az)) {
#if IMU_DEBUG_SCREEN
            if ((++dbg % 15) == 0) {
                ESP_LOGI(TAG, "accel(x100): x=%d y=%d z=%d",
                         (int)(ax * 100), (int)(ay * 100), (int)(az * 100));
            }
#endif
            if (maze_active()) {
                maze_tilt(ax, ay);
            } else if (g2048_active()) {
                g2048_tilt(ax, ay);
            } else if (ui_article_visible()) {
                float axis = (IMU_TILT_AXIS == 0) ? ax : (IMU_TILT_AXIS == 1) ? ay : az;
                float over = fabsf(axis) - IMU_TILT_DEADZONE;
                if (over > 0.0f) {
                    float dir = (axis > 0 ? 1.0f : -1.0f) * (IMU_TILT_INVERT ? -1.0f : 1.0f);
                    residual += dir * over * IMU_SCROLL_GAIN * (IMU_POLL_MS / 1000.0f);
                    int step = (int)residual;
                    if (step != 0) { ui_scroll_px(step); residual -= step; }
                } else {
                    residual = 0.0f;
                }
            } else {
#if IMU_DEBUG_SCREEN
                imu_show_readout(ax, ay, az);
#endif
            }
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_POLL_MS));
    }
}
#endif

// ---------------------------------------------------------------------------
//  Live (microphone) path
// ---------------------------------------------------------------------------
#if !TEST_NO_MIC
static void handle_press(void)
{
    ui_show_status("Listening...");
    if (!s_speech_ok) { do_random(); return; }
    int cmd = speech_listen(4000);
    ESP_LOGI(TAG, "recognised id: %d", cmd);
    if (cmd < 0) { ui_show_spelling(s_spell); return; }
    handle_command(cmd);
}
#endif

// ---------------------------------------------------------------------------
//  NO-MIC test path: type commands into the serial monitor
// ---------------------------------------------------------------------------
#if TEST_NO_MIC
static void console_handle_line(char *line)
{
    for (char *p = line; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (line[0] == '\0') return;

    // --- game commands take over while a game is running ---
    if (maze_active() || g2048_active()) {
        if (!strcmp(line, "exit") || !strcmp(line, "wiki")) { exit_games(); return; }
        if (g2048_active()) {
            if (!strcmp(line, "w")) { g2048_move(0); return; }
            if (!strcmp(line, "s")) { g2048_move(1); return; }
            if (!strcmp(line, "a")) { g2048_move(2); return; }
            if (!strcmp(line, "d")) { g2048_move(3); return; }
            if (!strcmp(line, "r")) { g2048_restart(); return; }
        }
        printf("[GAME] in-game: %s | exit\n",
               g2048_active() ? "w/a/s/d move, r restart" : "tilt to roll");
        return;
    }
    if (!strncmp(line, "maze", 4)) {      // "maze" or "maze 12"
        int n = atoi(line + 4);
        maze_start(n > 0 ? n : GAME_DEFAULT_MAZE_N);
        return;
    }
    if (!strncmp(line, "2048", 4)) {      // "2048" or "2048 5"
        int n = atoi(line + 4);
        g2048_start(n > 0 ? n : GAME_DEFAULT_2048_N);
        return;
    }

    if (line[0] == '=') {                 // "=black holes" -> search that text directly
        strncpy(s_spell, line + 1, sizeof(s_spell) - 1);
        s_spell[sizeof(s_spell) - 1] = '\0';
        s_spell_len = (int)strlen(s_spell);
        do_search();
        return;
    }
    if (!strcmp(line, "go"))     { handle_command(CMD_SEARCH); return; }
    if (!strcmp(line, "random")) { handle_command(CMD_RANDOM); return; }
    if (!strcmp(line, "back"))   { handle_command(CMD_BACK);   return; }
    if (line[1] == '\0' && line[0] >= 'a' && line[0] <= 'z') {
        handle_command(line[0] - 'a' + 1);   // single letter
        return;
    }
    int id = speech_phrase_to_id(line);      // NATO words + registered controls
    if (id > 0) { handle_command(id); return; }

    printf("[TEST] unknown: '%s'\n"
           "  try: =neptune | alpha..zulu | a..z | search | delete | clear | space | random | back | go\n",
           line);
}

static void console_task(void *arg)
{
    setvbuf(stdin, NULL, _IONBF, 0);
    uart_vfs_dev_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    uart_vfs_dev_use_driver(UART_NUM_0);
    vTaskDelay(pdMS_TO_TICKS(300));

    printf("\n===== espai NO-MIC TEST MODE =====\n"
           "Type a line, press Enter:\n"
           "  =neptune       search text directly (fastest test)\n"
           "  alpha..zulu    add one letter the NATO way\n"
           "  a..z           add one letter\n"
           "  search / go    look up the spelled word\n"
           "  delete clear space random back\n"
           "  maze [N]       tilt-a-ball maze (N=4..14 cells)\n"
           "  2048 [N]       2048 by tilting (N=3..6 board)\n"
           "  exit           leave a game (or press the button)\n"
           "==================================\n");

    char line[96];
    while (1) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            size_t n = strlen(line);
            while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' '))
                line[--n] = '\0';
            if (n > 0) console_handle_line(line);
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
#endif // TEST_NO_MIC

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ui_init();
    ui_show_status("Connecting\nWi-Fi...");

    button_init();
#if !TEST_NO_MIC
    if (!mic_init()) ui_show_status("Mic init failed");
#endif

#if IMU_ENABLE
    s_imu_ok = imu_init();
    // Create the task either way: if the IMU is missing it shows that on screen.
    xTaskCreate(imu_scroll_task, "imu", 6144, NULL, 4, NULL);   // also runs game ticks
    ESP_LOGI(TAG, "IMU %s", s_imu_ok ? "ready (tilt-to-scroll on)" : "NOT FOUND");
#endif

    if (!wifi_conn_start()) {
        ui_show_status("Wi-Fi failed.\nCheck SSID/pass\nin app_config.h");
    } else {
        ui_show_status("Loading speech\nmodel...");
    }

    gallery_start();               // background image loader

    s_speech_ok = speech_init();   // loads the model from flash (no mic needed)

#if TEST_NO_MIC
    ui_show_status("TEST MODE\n(no microphone)\n\nType commands in\nthe serial monitor.");
    xTaskCreate(console_task, "console", 12288, NULL, 5, NULL);   // room for TLS

    // Console task does the work; the button exits a running game.
    bool was_down = false;
    while (1) {
        bool down = button_pressed();
        if (down && !was_down && (maze_active() || g2048_active())) {
            vTaskDelay(pdMS_TO_TICKS(30));   // debounce
            if (button_pressed()) exit_games();
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
#else
    if (s_speech_ok) ui_show_spelling(s_spell);
    else ui_show_status("Speech model not\nloaded. Button =\nrandom article.");

    bool was_down = false;
    while (1) {
        bool down = button_pressed();
        if (down && !was_down) {
            vTaskDelay(pdMS_TO_TICKS(30));   // debounce
            if (button_pressed()) handle_press();
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif
}
