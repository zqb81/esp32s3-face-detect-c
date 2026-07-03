/**
 * voice_i2s.h — I2S microphone and speaker driver for ESP32-S3
 */
#ifndef VOICE_I2S_H
#define VOICE_I2S_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t voice_mic_init(void);
void voice_mic_deinit(void);
esp_err_t voice_mic_read(uint8_t *buf, size_t len, size_t *bytes_read, uint32_t timeout_ms);

esp_err_t voice_spk_init(void);
void voice_spk_deinit(void);
esp_err_t voice_spk_write(const uint8_t *buf, size_t len, size_t *bytes_written, uint32_t timeout_ms);

#endif // VOICE_I2S_H
