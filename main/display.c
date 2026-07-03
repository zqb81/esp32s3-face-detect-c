/**
 * TFT display driver implementation - ST7735 via SPI
 */

#include "display.h"
#include "config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display";
static spi_device_handle_t spi = NULL;
static uint8_t *s_dma_bounce = NULL;

#define TFT_CHUNK_ROWS 40
#define TFT_CHUNK_BYTES (TFT_WIDTH * TFT_CHUNK_ROWS * 2)

// ST7735 128x160 column/row offsets (0,0 matches the working MicroPython version)
#define TFT_COL_OFFSET  0
#define TFT_ROW_OFFSET  0

static void tft_cmd(uint8_t cmd, const uint8_t *data, int len)
{
    gpio_set_level(TFT_PIN_CS, 0);
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
    gpio_set_level(TFT_PIN_CS, 1);
}

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    uint16_t cx0 = x0 + TFT_COL_OFFSET;
    uint16_t cx1 = x1 + TFT_COL_OFFSET;
    uint16_t ry0 = y0 + TFT_ROW_OFFSET;
    uint16_t ry1 = y1 + TFT_ROW_OFFSET;

    data[0] = cx0 >> 8;
    data[1] = cx0 & 0xFF;
    data[2] = cx1 >> 8;
    data[3] = cx1 & 0xFF;
    tft_cmd(0x2A, data, 4);  // CASET

    data[0] = ry0 >> 8;
    data[1] = ry0 & 0xFF;
    data[2] = ry1 >> 8;
    data[3] = ry1 & 0xFF;
    tft_cmd(0x2B, data, 4);  // RASET

    tft_cmd(0x2C, NULL, 0);  // RAMWR
}

esp_err_t display_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask =
            (1ULL << TFT_PIN_CS) | (1ULL << TFT_PIN_DC) | (1ULL << TFT_PIN_RST) | (1ULL << TFT_PIN_BL),
    };
    gpio_config(&io_conf);

    gpio_set_level(TFT_PIN_CS, 1);
    // Try backlight on immediately (some modules are active-low)
    gpio_set_level(TFT_PIN_BL, 1);
    ESP_LOGI(TAG, "BL pin %d set HIGH", TFT_PIN_BL);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = TFT_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = TFT_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_CHUNK_BYTES + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = TFT_SPI_FREQ,
        .mode = 0,
        .spics_io_num = -1,  // Manual CS — hardware CS toggles between cmd/data
        .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(TFT_SPI_HOST, &dev_cfg, &spi));

    s_dma_bounce = heap_caps_malloc(TFT_CHUNK_BYTES, MALLOC_CAP_DMA);
    if (!s_dma_bounce) {
        ESP_LOGE(TAG, "DMA bounce buffer alloc failed");
        spi_bus_remove_device(spi);
        spi = NULL;
        spi_bus_free(TFT_SPI_HOST);
        return ESP_ERR_NO_MEM;
    }

    // Hardware reset
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Software reset
    tft_cmd(0x01, NULL, 0);  // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));

    // Sleep out
    tft_cmd(0x11, NULL, 0);  // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(255));

    // Frame rate control (normal/idle/partial)
    {
        uint8_t d[] = {0x01, 0x2C, 0x2D};
        tft_cmd(0xB1, d, 3);  // FRMCTR1
        tft_cmd(0xB2, d, 3);  // FRMCTR2
    }
    {
        uint8_t d[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
        tft_cmd(0xB3, d, 6);  // FRMCTR3
    }

    // Display inversion control
    {
        uint8_t d[] = {0x07};
        tft_cmd(0xB4, d, 1);  // INVCTR
    }

    // Power control
    {
        uint8_t d[] = {0xA2, 0x02, 0x84};
        tft_cmd(0xC0, d, 3);  // PWCTR1
    }
    {
        uint8_t d[] = {0xC5};
        tft_cmd(0xC1, d, 1);  // PWCTR2
    }
    {
        uint8_t d[] = {0x0A, 0x00};
        tft_cmd(0xC2, d, 2);  // PWCTR3
    }
    {
        uint8_t d[] = {0x8A, 0x2A};
        tft_cmd(0xC3, d, 2);  // PWCTR4
    }
    {
        uint8_t d[] = {0x8A, 0xEE};
        tft_cmd(0xC4, d, 2);  // PWCTR5
    }

    // VCOM control
    {
        uint8_t d[] = {0x0E};
        tft_cmd(0xC5, d, 1);  // VMCTR1
    }

    // Display inversion off (try INVON=0x21 if colors look wrong)
    tft_cmd(0x20, NULL, 0);  // INVOFF

    // Color mode: 16-bit
    {
        uint8_t d[] = {0x05};
        tft_cmd(0x3A, d, 1);  // COLMOD
    }

    // Memory access control (rotation)
    {
        uint8_t d[] = {0xC0};  // MY=1, MX=1 → 180° rotation
        tft_cmd(0x36, d, 1);  // MADCTL
    }

    // Gamma positive
    {
        uint8_t d[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                       0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
        tft_cmd(0xE0, d, 16);  // GMCTRP1
    }
    // Gamma negative
    {
        uint8_t d[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                       0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
        tft_cmd(0xE1, d, 16);  // GMCTRN1
    }

    // Normal display on
    tft_cmd(0x13, NULL, 0);  // NORON
    vTaskDelay(pdMS_TO_TICKS(10));

    // Display on
    tft_cmd(0x29, NULL, 0);  // DISPON
    vTaskDelay(pdMS_TO_TICKS(100));

    // Backlight on
    gpio_set_level(TFT_PIN_BL, 1);

    // Use LOGW so it's visible (INFO level is filtered in this build)
    ESP_LOGW(TAG, "TFT init done. BL=GPIO%d HIGH. SPI=%d. Pins: SCK=%d MOSI=%d CS=%d DC=%d RST=%d",
             TFT_PIN_BL, TFT_SPI_HOST, TFT_PIN_SCK, TFT_PIN_MOSI, TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);
    return ESP_OK;
}

void display_draw_frame(const uint8_t *buf)
{
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    gpio_set_level(TFT_PIN_CS, 0);
    gpio_set_level(TFT_PIN_DC, 1);
    for (int y = 0; y < TFT_HEIGHT; y += TFT_CHUNK_ROWS) {
        int rows = TFT_CHUNK_ROWS;
        if (y + rows > TFT_HEIGHT) {
            rows = TFT_HEIGHT - y;
        }

        int chunk_bytes = TFT_WIDTH * rows * 2;
        memcpy(s_dma_bounce, buf + y * TFT_WIDTH * 2, chunk_bytes);

        spi_transaction_t t = {
            .length = chunk_bytes * 8,
            .tx_buffer = s_dma_bounce,
        };
        spi_device_polling_transmit(spi, &t);
    }
    gpio_set_level(TFT_PIN_CS, 1);
}

void display_draw_rect(uint8_t *buf, int x, int y, int w, int h, uint16_t color)
{
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    for (int i = x; i < x + w; i++) {
        if (i >= 0 && i < TFT_WIDTH) {
            if (y >= 0 && y < TFT_HEIGHT) {
                int off = (y * TFT_WIDTH + i) * 2;
                buf[off] = hi;
                buf[off + 1] = lo;
            }
            int by = y + h - 1;
            if (by >= 0 && by < TFT_HEIGHT) {
                int off = (by * TFT_WIDTH + i) * 2;
                buf[off] = hi;
                buf[off + 1] = lo;
            }
        }
    }

    for (int j = y; j < y + h; j++) {
        if (j >= 0 && j < TFT_HEIGHT) {
            if (x >= 0 && x < TFT_WIDTH) {
                int off = (j * TFT_WIDTH + x) * 2;
                buf[off] = hi;
                buf[off + 1] = lo;
            }
            int rx = x + w - 1;
            if (rx >= 0 && rx < TFT_WIDTH) {
                int off = (j * TFT_WIDTH + rx) * 2;
                buf[off] = hi;
                buf[off + 1] = lo;
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
    free(s_dma_bounce);
    s_dma_bounce = NULL;
    spi_bus_remove_device(spi);
    spi = NULL;
    spi_bus_free(TFT_SPI_HOST);
}
