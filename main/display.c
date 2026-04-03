/**
 * TFT 显示驱动实现 - ST7735 via SPI
 */

#include "display.h"
#include "config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display";
static spi_device_handle_t spi = NULL;

// ===== 底层 SPI =====
static void tft_cmd(uint8_t cmd, const uint8_t *data, int len)
{
    gpio_set_level(TFT_PIN_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi, &t);

    if (data && len > 0) {
        gpio_set_level(TFT_PIN_DC, 1);
        t.length = len * 8;
        t.tx_buffer = data;
        spi_device_polling_transmit(spi, &t);
    }
}

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    data[0] = x0 >> 8; data[1] = x0 & 0xFF;
    data[2] = x1 >> 8; data[3] = x1 & 0xFF;
    tft_cmd(0x2A, data, 4);

    data[0] = y0 >> 8; data[1] = y0 & 0xFF;
    data[2] = y1 >> 8; data[3] = y1 & 0xFF;
    tft_cmd(0x2B, data, 4);

    tft_cmd(0x2C, NULL, 0);
}

esp_err_t display_init(void)
{
    // GPIO
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_PIN_CS) | (1ULL << TFT_PIN_DC) |
                        (1ULL << TFT_PIN_RST) | (1ULL << TFT_PIN_BL),
    };
    gpio_config(&io_conf);

    gpio_set_level(TFT_PIN_CS, 1);
    gpio_set_level(TFT_PIN_BL, 0);

    // SPI
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = TFT_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = TFT_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2 + 8,
    };
    spi_bus_initialize(TFT_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = TFT_SPI_FREQ,
        .mode = 0,
        .spics_io_num = TFT_PIN_CS,
        .queue_size = 7,
    };
    spi_bus_add_device(TFT_SPI_HOST, &dev_cfg, &spi);

    // 复位
    gpio_set_level(TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // 初始化序列
    tft_cmd(0x01, NULL, 0);  // Software reset
    vTaskDelay(pdMS_TO_TICKS(150));
    tft_cmd(0x11, NULL, 0);  // Sleep out
    vTaskDelay(pdMS_TO_TICKS(255));

    uint8_t colmod = 0x05;  // 16-bit color
    tft_cmd(0x3A, &colmod, 1);

    uint8_t madctl = 0xC0;  // MY + MX
    tft_cmd(0x36, &madctl, 1);

    tft_cmd(0x29, NULL, 0);  // Display on

    gpio_set_level(TFT_PIN_BL, 1);

    ESP_LOGI(TAG, "TFT OK (128x160)");
    return ESP_OK;
}

void display_draw_frame(const uint8_t *buf)
{
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    gpio_set_level(TFT_PIN_DC, 1);
    spi_transaction_t t = {
        .length = TFT_WIDTH * TFT_HEIGHT * 2 * 8,
        .tx_buffer = buf,
    };
    spi_device_polling_transmit(spi, &t);
}

void display_draw_rect(uint8_t *buf, int x, int y, int w, int h, uint16_t color)
{
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    // 上下边
    for (int i = x; i < x + w; i++) {
        if (i >= 0 && i < TFT_WIDTH) {
            if (y >= 0 && y < TFT_HEIGHT) {
                int off = (y * TFT_WIDTH + i) * 2;
                buf[off] = hi; buf[off + 1] = lo;
            }
            int by = y + h - 1;
            if (by >= 0 && by < TFT_HEIGHT) {
                int off = (by * TFT_WIDTH + i) * 2;
                buf[off] = hi; buf[off + 1] = lo;
            }
        }
    }
    // 左右边
    for (int j = y; j < y + h; j++) {
        if (j >= 0 && j < TFT_HEIGHT) {
            if (x >= 0 && x < TFT_WIDTH) {
                int off = (j * TFT_WIDTH + x) * 2;
                buf[off] = hi; buf[off + 1] = lo;
            }
            int rx = x + w - 1;
            if (rx >= 0 && rx < TFT_WIDTH) {
                int off = (j * TFT_WIDTH + rx) * 2;
                buf[off] = hi; buf[off + 1] = lo;
            }
        }
    }
}

void display_backlight(bool on)
{
    gpio_set_level(TFT_PIN_BL, on ? 1 : 0);
}

void display_deinit(void)
{
    display_backlight(false);
    spi_bus_remove_device(spi);
    spi_bus_free(TFT_SPI_HOST);
}
