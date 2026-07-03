/**
 * device_ctrl.h — Device control drivers for ESP32-S3
 * Motor, LED, RGB, Buzzer, DHT, Light sensor
 */
#ifndef DEVICE_CTRL_H
#define DEVICE_CTRL_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t device_ctrl_init_all(void);

// Motor/Fan
void device_motor_on(int speed);
void device_motor_off(void);

// LED
void device_led_on(void);
void device_led_off(void);

// RGB (WS2812)
void device_rgb_set(int r, int g, int b);
void device_rgb_color(const char *name);

// Buzzer
void device_buzzer_on(void);
void device_buzzer_off(void);
void device_buzzer_beep(int times);

// DHT sensor
esp_err_t device_dht_read(float *temp, float *humi);

// Light sensor
int device_light_read(void);

#endif // DEVICE_CTRL_H
