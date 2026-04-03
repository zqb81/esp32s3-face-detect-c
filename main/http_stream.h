/**
 * HTTP MJPEG 视频流
 */

#ifndef HTTP_STREAM_H
#define HTTP_STREAM_H

#include "esp_err.h"
#include <stdint.h>

/**
 * 启动 HTTP 流服务器
 * 提供 / (播放页) 和 /stream (MJPEG流)
 */
esp_err_t http_stream_start(int port);

/**
 * 更新最新帧 (JPEG)
 * @param jpeg_data JPEG 帧数据
 * @param len 数据长度
 */
void http_stream_update_frame(const uint8_t *jpeg_data, size_t len);

/**
 * 停止服务器
 */
void http_stream_stop(void);

/**
 * 上传 JPEG 帧到远程服务器
 * @param jpeg_data JPEG 数据
 * @param len 数据长度
 * @param interval_ms 最短上传间隔 (ms)
 */
void http_stream_upload(const uint8_t *jpeg_data, size_t len);

#endif // HTTP_STREAM_H
