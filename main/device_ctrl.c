/**
 * device_ctrl.c — Device control drivers
 * Adapted for ESP32-S3 with octal PSRAM (only GPIO 0/38/46/48 free)
 */
#include "device_ctrl.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "config.h"

static const char *TAG = "device_ctrl";

// ===== Motor — disabled (no free GPIO) =====

void device_motor_on(int speed)
{
    (void)speed;
    ESP_LOGW(TAG, "Motor not available (no free GPIO)");
}

void device_motor_off(void)
{
}

// ===== LED — disabled (no free GPIO) =====

void device_led_on(void)
{
    ESP_LOGW(TAG, "LED not available (no free GPIO)");
}

void device_led_off(void)
{
}

// ===== Buzzer (LEDC PWM on GPIO 48) =====

void device_buzzer_on(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void device_buzzer_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void device_buzzer_beep(int times)
{
    for (int i = 0; i < times; i++) {
        device_buzzer_on();
        vTaskDelay(pdMS_TO_TICKS(200));
        device_buzzer_off();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ===== DHT — disabled (no free GPIO) =====

esp_err_t device_dht_read(float *temp, float *humi)
{
    *temp = 0;
    *humi = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

// ===== Light Sensor — disabled (no free ADC GPIO) =====

int device_light_read(void)
{
    return -1;
}

// ===== RGB (WS2812 via RMT on GPIO 38) =====

static rmt_channel_handle_t s_rgb_chan = NULL;
static rmt_encoder_handle_t s_rgb_encoder = NULL;

// 10 MHz clock: 1 tick = 100 ns
#define RMT_RES_HZ        10000000
#define WS2812_T0H_TICKS  4    // 400 ns
#define WS2812_T0L_TICKS  8    // 800 ns
#define WS2812_T1H_TICKS  8    // 800 ns
#define WS2812_T1L_TICKS  4    // 400 ns

static void rgb_encode_bit(rmt_symbol_word_t *sym, int color_bit)
{
    if (color_bit) {
        sym->level0 = 1; sym->duration0 = WS2812_T1H_TICKS;
        sym->level1 = 0; sym->duration1 = WS2812_T1L_TICKS;
    } else {
        sym->level0 = 1; sym->duration0 = WS2812_T0H_TICKS;
        sym->level1 = 0; sym->duration1 = WS2812_T0L_TICKS;
    }
}

static void rgb_send(int r, int g, int b)
{
    if (!s_rgb_chan || !s_rgb_encoder) return;

    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };

    for (int led = 0; led < DEVICE_RGB_NUM; led++) {
        rmt_symbol_word_t symbols[24];
        for (int i = 0; i < 8; i++) {
            rgb_encode_bit(&symbols[i],      (g >> (7 - i)) & 1);
            rgb_encode_bit(&symbols[8 + i],  (r >> (7 - i)) & 1);
            rgb_encode_bit(&symbols[16 + i], (b >> (7 - i)) & 1);
        }
        rmt_transmit(s_rgb_chan, s_rgb_encoder, symbols, sizeof(symbols), &tx_cfg);
        rmt_tx_wait_all_done(s_rgb_chan, 100);
    }
}

void device_rgb_set(int r, int g, int b)
{
    rgb_send(r, g, b);
}

void device_rgb_color(const char *name)
{
    struct { const char *name; int r, g, b; } colors[] = {
        {"red", 255, 0, 0}, {"green", 0, 255, 0}, {"blue", 0, 0, 255},
        {"yellow", 255, 255, 0}, {"purple", 128, 0, 128}, {"white", 255, 255, 255},
        {"off", 0, 0, 0}, {"idle", 0, 0, 10},
    };
    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i++) {
        if (strcmp(name, colors[i].name) == 0) {
            device_rgb_set(colors[i].r, colors[i].g, colors[i].b);
            return;
        }
    }
    ESP_LOGW(TAG, "Unknown RGB color: %s", name);
}

// ===== Init =====

esp_err_t device_ctrl_init_all(void)
{
    ESP_LOGW(TAG, "Init: buzzer=GPIO%d, RGB=GPIO%d (motor/LED/DHT/light disabled)",
             DEVICE_BUZZER_PIN, DEVICE_RGB_PIN);

    // Buzzer PWM (LEDC)
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BUZZER_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t buzzer_ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = DEVICE_BUZZER_PIN,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&buzzer_ch));

    // RGB LED (WS2812 via RMT)
    rmt_tx_channel_config_t rgb_chan_cfg = {
        .gpio_num = DEVICE_RGB_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&rgb_chan_cfg, &s_rgb_chan);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RMT channel failed: %s", esp_err_to_name(ret));
    } else {
        rmt_copy_encoder_config_t copy_enc_cfg = {};
        rmt_new_copy_encoder(&copy_enc_cfg, &s_rgb_encoder);
        rmt_enable(s_rgb_chan);
    }

    ESP_LOGW(TAG, "Device control ready");
    return ESP_OK;
}
