// ---------------------------------------------------------------------------
// app_config.h - All board pins and user settings in ONE place.
// Board: Waveshare ESP32-S3-LCD-1.3  (ESP32-S3R8)
// ---------------------------------------------------------------------------
#pragma once

// ===========================================================================
//  Wi-Fi credentials  --  EDIT THESE TWO LINES
// ===========================================================================
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// ===========================================================================
//  LCD (ST7789V2, 240x240, on SPI2/FSPI) -- fixed by the board wiring.
//  Verified from the Waveshare demo code; do not change unless you rewire.
// ===========================================================================
#define LCD_SPI_HOST     SPI2_HOST
#define LCD_PIN_SCLK     40
#define LCD_PIN_MOSI     41
#define LCD_PIN_DC       38
#define LCD_PIN_CS       39
#define LCD_PIN_RST      42
#define LCD_PIN_BL       20      // backlight
#define LCD_H_RES        240
#define LCD_V_RES        240
#define LCD_PIXEL_CLK_HZ (40 * 1000 * 1000)

// ===========================================================================
//  INMP441 microphone (I2S) -- wired to the FREE header pins.
//  These three GPIOs are on the left 2.54mm header next to 3V3/GND.
//  (GPIO3 is skipped on purpose: it is an ESP32-S3 strapping pin.)
//
//  INMP441 pin -> ESP32-S3 pin
//     VDD  -> 3V3(OUT)
//     GND  -> GND
//     L/R  -> GND          (selects the LEFT I2S slot)
//     WS   -> GPIO2        (word select / LRCL)
//     SCK  -> GPIO4        (bit clock / BCLK)
//     SD   -> GPIO1        (serial data out of mic -> data in to ESP)
// ===========================================================================
#define MIC_I2S_PORT     I2S_NUM_0
#define MIC_PIN_WS       2
#define MIC_PIN_BCLK     4
#define MIC_PIN_SD       1
#define MIC_SAMPLE_RATE  16000   // 16 kHz mono - what ESP-SR expects
// INMP441 gives 24-bit data left-justified in a 32-bit slot. We shift it down
// to 16-bit PCM. A smaller shift = louder. 14 is a good start; if speech is too
// quiet for recognition try 13, if it clips/distorts try 15 or 16.
#define MIC_GAIN_SHIFT   14

// ===========================================================================
//  Push-to-talk button.
//  Default = the onboard BOOT button (GPIO0) so you need NO extra parts.
//  Pressed == logic LOW. (You may instead wire a momentary button from any
//  free header GPIO to GND and set BUTTON_PIN to it.)
// ===========================================================================
#define BUTTON_PIN       0       // onboard BOOT button
#define BUTTON_ACTIVE_LOW 1

// ===========================================================================
//  IMU (QMI8658, onboard) -- tilt the board up/down to scroll the article.
//  Fixed board wiring: I2C SDA=47, SCL=48.
// ===========================================================================
#define IMU_ENABLE       1
#define IMU_PIN_SDA      47
#define IMU_PIN_SCL      48
// Which accelerometer axis reacts to tilting up/down (0=X, 1=Y, 2=Z).
// If tilting up/down does nothing but tilting left/right scrolls, change this.
#define IMU_TILT_AXIS    1
// Set to 1 if scrolling goes the wrong way (tilt up scrolls down).
#define IMU_TILT_INVERT  1
// How far you must tilt before it scrolls (in g; ~0.2 = about 12 degrees).
#define IMU_TILT_DEADZONE 0.20f
// Scroll speed: pixels/second per g of tilt beyond the dead zone.
#define IMU_SCROLL_GAIN  900.0f
#define IMU_POLL_MS      30
// Live accelerometer readout on the LCD + serial (was for bring-up). 0 = off.
#define IMU_DEBUG_SCREEN 0

// ===========================================================================
//  Games (maze + 2048), controlled by tilting the board.
// ===========================================================================
// Default grid sizes ("resolution"). Override at runtime: "maze 12", "2048 5".
#define GAME_DEFAULT_MAZE_N  8    // maze cells per side (4..14)
#define GAME_DEFAULT_2048_N  4    // 2048 board size (3..6)
// Flip these if the ball / tiles move the wrong way when you tilt.
#define GAME_INV_X           0
#define GAME_INV_Y           1    // default matches the article-scroll direction

// ===========================================================================
//  Wikipedia language edition (e.g. "en", "es", "fr", "pt").
// ===========================================================================
#define WIKI_LANG        "en"

// ===========================================================================
//  NO-MIC TEST MODE
//  Set to 1 to test WITHOUT the microphone: the mic is skipped and you drive
//  everything by TYPING into the `idf.py monitor` serial terminal instead of
//  speaking. Set back to 0 once your INMP441 arrives.
// ===========================================================================
#define TEST_NO_MIC      1
