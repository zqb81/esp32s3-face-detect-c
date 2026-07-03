/**
 * voice_i2s.c — I2S microphone (INMP441) and speaker (MAX98357A) driver
 */
#include "voice_i2s.h"

#include <string.h>
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "config.h"

static const char *TAG = "voice_i2s";

static i2s_chan_handle_t s_mic_chan = NULL;
static i2s_chan_handle_t s_spk_chan = NULL;

// ===== Microphone (RX) =====

esp_err_t voice_mic_init(void)
{
    if (s_mic_chan) {
        ESP_LOGW(TAG, "Mic already initialized");
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 256;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_mic_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_GPIO_UNUSED,
            .ws = I2S_GPIO_UNUSED,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    std_cfg.gpio_cfg.bclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.ws = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_mic_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_mic_chan));

    ESP_LOGI(TAG, "Microphone initialized (I2S0 RX, %dHz)", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

void voice_mic_deinit(void)
{
    if (s_mic_chan) {
        i2s_channel_disable(s_mic_chan);
        i2s_del_channel(s_mic_chan);
        s_mic_chan = NULL;
        ESP_LOGI(TAG, "Microphone deinitialized");
    }
}

esp_err_t voice_mic_read(uint8_t *buf, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!s_mic_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(s_mic_chan, buf, len, bytes_read, timeout_ms);
}

// ===== Speaker (TX) =====

esp_err_t voice_spk_init(void)
{
    if (s_spk_chan) {
        ESP_LOGW(TAG, "Speaker already initialized");
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 256;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_spk_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_GPIO_UNUSED,
            .ws = I2S_GPIO_UNUSED,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    std_cfg.gpio_cfg.bclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.ws = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_spk_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_spk_chan));

    ESP_LOGI(TAG, "Speaker initialized (I2S1 TX, %dHz)", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

void voice_spk_deinit(void)
{
    if (s_spk_chan) {
        i2s_channel_disable(s_spk_chan);
        i2s_del_channel(s_spk_chan);
        s_spk_chan = NULL;
        ESP_LOGI(TAG, "Speaker deinitialized");
    }
}

esp_err_t voice_spk_write(const uint8_t *buf, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!s_spk_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_write(s_spk_chan, buf, len, bytes_written, timeout_ms);
}
