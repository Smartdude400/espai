#include "audio_mic.h"
#include "app_config.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

static const char *TAG = "mic";
static i2s_chan_handle_t s_rx;

bool mic_init(void)
{
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, NULL, &s_rx) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed");
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_PIN_BCLK,
            .ws   = MIC_PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_PIN_SD,
            .invert_flags = { 0 },
        },
    };
    // INMP441 has L/R tied to GND -> it drives data in the LEFT slot only.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    if (i2s_channel_init_std_mode(s_rx, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "i2s init std failed");
        return false;
    }
    if (i2s_channel_enable(s_rx) != ESP_OK) {
        ESP_LOGE(TAG, "i2s enable failed");
        return false;
    }
    ESP_LOGI(TAG, "INMP441 ready (BCLK=%d WS=%d SD=%d)",
             MIC_PIN_BCLK, MIC_PIN_WS, MIC_PIN_SD);
    return true;
}

size_t mic_read(int16_t *dst, size_t max_samples)
{
    if (!s_rx) return 0;

    // INMP441 sends 24-bit data left-justified in a 32-bit slot.
    int32_t raw[256];
    size_t got = 0;
    while (got < max_samples) {
        size_t want = max_samples - got;
        if (want > 256) want = 256;
        size_t bytes_read = 0;
        if (i2s_channel_read(s_rx, raw, want * sizeof(int32_t),
                             &bytes_read, pdMS_TO_TICKS(200)) != ESP_OK) {
            break;
        }
        size_t n = bytes_read / sizeof(int32_t);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            // 24-bit sample (left-justified in 32 bits) -> 16-bit PCM, with gain + clamp.
            int32_t s = raw[i] >> MIC_GAIN_SHIFT;
            if (s > 32767) s = 32767;
            else if (s < -32768) s = -32768;
            dst[got + i] = (int16_t)s;
        }
        got += n;
        if (n < want) break;
    }
    return got;
}

int mic_measure_level(int ms)
{
    const int chunk = 256;
    int16_t buf[256];
    int total = MIC_SAMPLE_RATE * ms / 1000;
    int peak = 0;
    while (total > 0) {
        size_t n = mic_read(buf, total > chunk ? chunk : total);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            int a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > peak) peak = a;
        }
        total -= n;
    }
    return peak;
}
