#include "imu.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "imu";

// --- QMI8658 registers ---
#define QMI_WHO_AM_I   0x00
#define QMI_CTRL1      0x02
#define QMI_CTRL2      0x03
#define QMI_CTRL7      0x08
#define QMI_RESET      0x60
#define QMI_RST_RESULT     0x4D   // reads 0x80 once a soft reset has finished
#define QMI_RST_RESULT_VAL 0x80
#define QMI_AX_L       0x35
#define QMI_WHOAMI_VAL 0x05
#define QMI_ACC_LSB_PER_G 16384.0f   // ±2g full scale

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_ready;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100);
}

bool imu_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = IMU_PIN_SCL,
        .sda_io_num = IMU_PIN_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        return false;
    }

    // The QMI8658 answers at 0x6B or 0x6A depending on the SA0 strap.
    uint8_t addr = 0;
    const uint8_t candidates[] = { 0x6B, 0x6A };
    for (int i = 0; i < 2; i++) {
        if (i2c_master_probe(s_bus, candidates[i], 50) == ESP_OK) { addr = candidates[i]; break; }
    }
    if (!addr) { ESP_LOGW(TAG, "no IMU on I2C (SDA=%d SCL=%d)", IMU_PIN_SDA, IMU_PIN_SCL); return false; }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) return false;

    uint8_t who = 0;
    reg_read(QMI_WHO_AM_I, &who, 1);
    if (who != QMI_WHOAMI_VAL) {
        ESP_LOGW(TAG, "unexpected WHO_AM_I 0x%02X at 0x%02X (expected 0x05)", who, addr);
        return false;
    }
    ESP_LOGI(TAG, "QMI8658 found at 0x%02X", addr);

    // Soft reset, then WAIT for the reset-complete flag (reg 0x4D -> 0x80).
    // Configuring too soon silently fails and the accel then outputs 0x8000
    // forever (reads as -2.0 g). This handshake is the key fix.
    reg_write(QMI_RESET, 0xB0);
    bool reset_ok = false;
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t r = 0;
        if (reg_read(QMI_RST_RESULT, &r, 1) == ESP_OK && r == QMI_RST_RESULT_VAL) {
            reset_ok = true;
            break;
        }
    }
    ESP_LOGI(TAG, "reset %s", reset_ok ? "complete" : "TIMEOUT (continuing anyway)");

    // CTRL1: enable register-address auto-increment (bit 6), preserve other bits.
    uint8_t c1 = 0;
    reg_read(QMI_CTRL1, &c1, 1);
    reg_write(QMI_CTRL1, c1 | (1 << 6));

    reg_write(QMI_CTRL7, 0x00);            // sensors off while configuring
    reg_write(QMI_CTRL2, 0x03);            // accel: ±2g, 1000 Hz ODR

    // CTRL7: enable accelerometer (bit 0), preserve other bits.
    uint8_t c7 = 0;
    reg_read(QMI_CTRL7, &c7, 1);
    reg_write(QMI_CTRL7, c7 | 0x01);
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t v2 = 0, v7 = 0;
    reg_read(QMI_CTRL2, &v2, 1);
    reg_read(QMI_CTRL7, &v7, 1);
    ESP_LOGI(TAG, "CTRL2=0x%02X CTRL7=0x%02X (accel %s)",
             v2, v7, (v7 & 0x01) ? "ON" : "OFF");

    s_ready = true;
    return true;
}

bool imu_read_accel_g(float *x, float *y, float *z)
{
    if (!s_ready) return false;
    uint8_t b[6];
    if (reg_read(QMI_AX_L, b, 6) != ESP_OK) return false;

    int16_t ax = (int16_t)(b[0] | (b[1] << 8));
    int16_t ay = (int16_t)(b[2] | (b[3] << 8));
    int16_t az = (int16_t)(b[4] | (b[5] << 8));

    *x = ax / QMI_ACC_LSB_PER_G;
    *y = ay / QMI_ACC_LSB_PER_G;
    *z = az / QMI_ACC_LSB_PER_G;
    return true;
}
