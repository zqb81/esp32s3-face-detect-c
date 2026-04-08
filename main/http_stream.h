/**
 * HTTP MJPEG streaming and async remote upload.
 */

#ifndef HTTP_STREAM_H
#define HTTP_STREAM_H

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Start the local HTTP MJPEG server and async upload worker.
 */
esp_err_t http_stream_start(int port);

/**
 * Update the latest JPEG frame used by the local /stream endpoint.
 */
void http_stream_update_frame(const uint8_t *jpeg_data, size_t len);

/**
 * Submit the latest JPEG frame to the async remote upload worker.
 *
 * The worker uses a latest-only strategy: if a newer frame arrives before the
 * previous one is uploaded, the older pending frame is dropped.
 */
void http_stream_submit_upload(const uint8_t *jpeg_data, size_t len);

/**
 * Stop the local server.
 */
void http_stream_stop(void);

#endif // HTTP_STREAM_H
