/**
 * voice_client.h — TCP voice client for ESP32-S3
 */
#ifndef VOICE_CLIENT_H
#define VOICE_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define MSG_AUDIO   0x01
#define MSG_CMD     0x02
#define MSG_DONE    0x03

typedef struct {
    int sock;
} voice_client_t;

esp_err_t voice_client_connect(voice_client_t *client, const char *host, uint16_t port);
void voice_client_disconnect(voice_client_t *client);

esp_err_t voice_send_frame(voice_client_t *client, uint8_t type, const uint8_t *data, size_t len);
esp_err_t voice_recv_frame(voice_client_t *client, uint8_t *type, uint8_t **data, size_t *len);

#endif // VOICE_CLIENT_H
