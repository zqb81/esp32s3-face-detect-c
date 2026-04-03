/**
 * TFT 显示驱动 - ST7735 128x160
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// RGB565 颜色
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_GREEN   0x07E0
#define COLOR_RED     0xF800
#define COLOR_YELLOW  0xFFE0
#define COLOR_BLUE    0x001F

/**
 * 初始化 TFT (SPI + GPIO)
 */
esp_err_t display_init(void);

/**
 * 写入整帧 RGB565 数据
 * @param buf 128*160*2 字节的 RGB565 buffer
 */
void display_draw_frame(const uint8_t *buf);

/**
 * 在 buffer 上画矩形框
 * @param buf 帧缓冲
 * @param x,y,w,h 坐标和尺寸
 * @param color RGB565 颜色
 */
void display_draw_rect(uint8_t *buf, int x, int y, int w, int h, uint16_t color);

/**
 * 开/关背光
 */
void display_backlight(bool on);

/**
 * 反初始化
 */
void display_deinit(void);

#endif // DISPLAY_H
