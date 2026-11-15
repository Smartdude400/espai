# espai — on-device Wikipedia voice gadget

A standalone **Waveshare ESP32-S3-LCD-1.3** gadget that listens through an
**INMP441** microphone, recognises speech **entirely on the chip**, and fetches
live articles from **Wikipedia** over Wi-Fi onto the 1.3" LCD. You search for
**any topic by spelling it** with the NATO alphabet — no PC, no cloud, no LLM.

---

## ⚠️ Read this first — what an ESP32 can and cannot do

This is the honest engineering reality, decided up front:

* **You cannot run a chatbot / LLM on this board.** 8 MB of PSRAM is ~100× too
  little for even the smallest useful language model, and "training" one is a
  data-center job, not a microcontroller job. Anything claiming otherwise is a toy.
* **Free-form speech-to-text cannot run on the chip either.** Turning an
  arbitrary spoken sentence ("how tall was Napoleon?") into text needs a
  Whisper-class model (hundreds of MB of RAM). The ESP32-S3 can only do
  **fixed command-word recognition** (Espressif ESP-SR, ~200 preset phrases).
* **What it genuinely does:** capture audio, recognise a **fixed vocabulary**
  on-device (here: the NATO alphabet + control words), and use Wi-Fi to pull
  **real, open Wikipedia content**. You reach *any* article by **spelling** the
  topic — open coverage without large-vocabulary speech-to-text.

So this is a **spell-to-search** Wikipedia reader, not a voice-*conversation*
assistant. That distinction is a hard limit of the hardware, not a software TODO.

---

## Hardware

| Part | Detail |
|---|---|
| Board | Waveshare ESP32-S3-LCD-1.3 (ESP32-S3R8: 8 MB octal PSRAM, 16 MB flash) |
| Display | 1.3" 240×240 ST7789V2, SPI2 |
| Microphone | INMP441 I2S MEMS mic |
| Trigger | onboard **BOOT** button (GPIO0) — no extra parts needed |

### Wiring the INMP441

All three signal pins go to the **left** 2.54 mm header (next to 3V3 / GND).
`GPIO3` is deliberately skipped (it is an ESP32-S3 strapping pin).

| INMP441 pin | Connect to | ESP32-S3 GPIO |
|---|---|---|
| VDD | 3V3 (OUT) | — |
| GND | GND | — |
| L/R | GND | — (selects the LEFT I2S slot) |
| WS  | header | **GPIO2** |
| SCK | header | **GPIO4** |
| SD  | header | **GPIO1** |

> Push-to-talk uses the onboard BOOT button by default. If you prefer a separate
> button, wire a momentary switch from any free header GPIO to GND and change
> `BUTTON_PIN` in `main/app_config.h`.

### Pins already used by the board (do not reuse)

| Function | GPIO |
|---|---|
| LCD DC / CS / SCK / MOSI / RST | 38 / 39 / 40 / 41 / 42 |
| LCD backlight | 20 |
| TF card (SDMMC) | 21 / 18 / 16 / 17 |
| IMU QMI8658 (I2C) | 47 / 48 |
| Octal PSRAM (internal) | 33–37 |

Free header GPIOs: **1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14**.

---

## Build & flash (Windows, ESP-IDF v5.3)

From a normal PowerShell:

```powershell
# 1. Activate ESP-IDF (once per shell)
$env:IDF_TOOLS_PATH = "C:\Espressif"
. C:\esp\v5.3\esp-idf\export.ps1

# 2. Set your Wi-Fi in main/app_config.h  (WIFI_SSID / WIFI_PASSWORD)

# 3. Build (from the project folder)
cd espai
idf.py build

# 4. Flash + watch the logs  (replace COMx with your board's port)
idf.py -p COMx flash monitor
```

(Find the port with Device Manager → Ports, or `mode` in cmd. Exit monitor with `Ctrl+]`.)

---

## Testing WITHOUT the microphone

Don't have the INMP441 yet? Set **`TEST_NO_MIC 1`** in [main/app_config.h](main/app_config.h)
(it ships enabled). The mic is skipped and you **type** the commands into the
`idf.py monitor` terminal — everything else (Wi-Fi, Wikipedia search, LCD
rendering, the spelling state machine) runs for real.

Flash + monitor, then type a line and press Enter:

| Type | Effect |
|---|---|
| `=black holes` | search that text directly (fastest end-to-end test) |
| `november` … / `a` … | add one letter (NATO word *or* single letter) |
| `search` or `go` | look up the spelled word |
| `delete` / `clear` / `space` | edit the spelled word |
| `random` / `back` | random article / previous article |

Example: `=neptune` <Enter> → Neptune's article appears on the LCD. Or spell it:
`november`↵ `echo`↵ `papa`↵ `tango`↵ `uniform`↵ `november`↵ `echo`↵ `search`↵.

**Turn it off** (`TEST_NO_MIC 0`) once the mic arrives to go back to voice.

---

## How it works — "spell any word" mode

You **spell** the topic one word per button-press using the **NATO alphabet**,
then say **"search"**. Everything is recognised on-chip; only the article text
comes from Wikipedia over Wi-Fi.

1. Boot → LCD up → Wi-Fi → load the on-device speech model → spelling screen.
2. **Hold the button, say ONE word, release.** Repeat to build the topic:
   * NATO letters `alpha…zulu` → add a letter (`november echo papa tango uniform november echo` = **NEPTUNE**)
   * `"delete"` → remove last letter, `"space"` → add a space, `"clear"` → erase
   * `"search"` → look it up; `"random article"` / `"go back"` → shortcuts
3. The spelled word goes to **Wikipedia's fuzzy search**, so small errors still
   land the right page (`nephtune` → **Neptune**). Title + summary fill the screen.

### Why NATO instead of "A, B, C"?
Spoken English letters (B/D/E/G/P/T/V/Z) are acoustically almost identical and a
small model mis-hears them constantly. NATO words (`bravo`, `delta`, `echo`…) are
designed to be distinct, which is what makes on-device spelling actually usable.

### Games 🎮 (tilt-controlled)
Two built-in games use the same IMU:

* **Maze** — type `maze` (or say *"play maze"*): tilt the board to roll the amber
  ball from the top-left to the green goal. Reaching it generates a fresh maze.
  Grid "resolution" is adjustable: `maze 12` → a denser 12×12 maze (4..14).
* **2048** — type `2048` (or say *"play puzzle"*): tip the board past ~20° to
  slide the tiles, level it, tilt again for the next move. Console keys
  `w/a/s/d` also move, `r` restarts. Board size: `2048 5` → 5×5 (3..6).
* **Exit** — type `exit`, press the BOOT button, or say *"exit game"*.

If the ball/tiles move the wrong way for how you hold the board, flip
`GAME_INV_X` / `GAME_INV_Y` in [main/app_config.h](main/app_config.h).

### Tilt to scroll 🌀
While an article is on screen, **tilt the board up/down to scroll** it (via the
onboard QMI8658 accelerometer). Tilt further = scroll faster; hold it level to
stop. The voice/typed `scroll up` / `scroll down` commands still work too.

Tuning in [main/app_config.h](main/app_config.h) (the firmware logs
`accel(x100): x/y/z` a few times a second, so you can see which axis reacts):
* `IMU_TILT_AXIS` — if up/down does nothing but *left/right* scrolls, change 0/1/2.
* `IMU_TILT_INVERT` — set 1 if it scrolls the wrong way.
* `IMU_TILT_DEADZONE` / `IMU_SCROLL_GAIN` — how far to tilt before it moves / how fast.

### Pictures 🖼️ (lazy-loaded gallery)
Images stack **below the text** and load **as you scroll toward the bottom** — a
background task ([wiki_image.c](main/wiki_image.c)) pulls the article's image list
from the media-list API and appends the next image whenever you get within a
screen of the end (up to `GAL_MAX`, currently 12). So the deeper you scroll, the
more photos appear.

The article's lead image shows **below the text** (scroll/tilt down to see it).
The top of the article also notes how many photos the page has, e.g.
`(3 photos below)`, so you know to scroll down.

* **JPEG** images are decoded on-chip to RGB565 ([wiki_image.c](main/wiki_image.c),
  via `esp_new_jpeg`) — fast to scroll past.
* **PNG** images are decoded by LVGL's lodepng. (Note: PNGs re-decode while you
  actively scroll past them, so scrolling can lag a moment on a PNG; it's sharp
  once still. Photos are usually JPEG anyway.)
* SVG images are skipped (text still shows).
* Photo count comes from the Wikipedia media-list API.

If JPEG colours look wrong, switch `JPEG_PIXEL_FORMAT_RGB565_LE` → `_BE` in
`wiki_image.c`.

### NATO chart
```
A alpha   B bravo    C charlie  D delta    E echo     F foxtrot
G golf    H hotel    I india    J juliet   K kilo     L lima
M mike    N november O oscar    P papa     Q quebec   R romeo
S sierra  T tango    U uniform  V victor   W whiskey  X x-ray
Y yankee  Z zulu
```

> Reality check: letter-by-letter voice spelling is inherently slower and less
> reliable than just typing — that's the price of "any word, fully standalone."
> If it feels too fiddly on your hardware, the local-Whisper-on-PC route gives
> smooth "say the whole word" search; ask and I'll wire it up.

### Source layout

| File | Responsibility |
|---|---|
| `main/app_config.h` | All pins + Wi-Fi + settings (edit this) |
| `main/wifi_conn.*`  | Wi-Fi station connect |
| `main/wikipedia.*`  | HTTPS GET to the Wikipedia REST API + JSON parse |
| `main/display_ui.*` | ST7789V2 + LVGL: status screen, title, scrollable body |
| `main/audio_mic.*`  | INMP441 I2S capture (16 kHz mono) |
| `main/speech_rec.*` | On-device ESP-SR MultiNet command recognition |
| `main/imu.*`        | QMI8658 accelerometer (tilt-to-scroll) |
| `main/wiki_image.*` | Download + JPEG-decode the article lead image |
| `main/main.c`       | Glue + push-to-talk / tilt state machine |

---

## On-device speech engine (ESP-SR)

The `espressif/esp-sr` **MultiNet** English model (`mn7_en`) runs on the chip. The
spoken vocabulary (26 NATO letters + `search`/`delete`/`clear`/`space` + a couple
of shortcuts) lives in `main/speech_rec.c` (`k_cmds[]`). The letter→character
mapping is `speech_id_to_letter()`; the spelling state machine is in `main/main.c`.

To add words, edit `k_cmds[]` (plain lowercase English; MultiNet derives the
pronunciation). Keep the total well under ~200 phrases.

### Model in flash — important for the first flash

The speech model is packed into the `model` partition automatically and flashed
by `idf.py flash`. The **whole** image (app + model) is flashed together when you
run `flash`. If you ever flash only the app, also flash the model once:

```powershell
idf.py -p COMx flash    # flashes app + partition + model
```

Tuning knobs if recognition is unreliable:
* Mic too quiet/loud → `MIC_GAIN_SHIFT` in `main/app_config.h` (lower = louder).
* Too many false / missed triggers → `set_det_threshold(...)` in `main/speech_rec.c`
  (higher = stricter, fewer false positives; lower = more sensitive).

---

## Limitations (by design / by hardware)

* Open search works by **spelling** (NATO), not free dictation — the chip can't
  run large-vocabulary speech-to-text. Spelling is slower but covers any word.
* Letter recognition accuracy depends on your mic/room; tune `MIC_GAIN_SHIFT`
  and the detection threshold. NATO words are used precisely to maximise it.
* Needs Wi-Fi (Wikipedia lives online). A fully-offline mode would require
  pre-loading article summaries into flash / SD.
* The default LVGL font renders Latin text well; some accented/non-Latin
  characters may show as boxes until a larger Unicode font is enabled.
