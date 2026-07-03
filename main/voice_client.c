/**
 * voice_client.c — TCP voice client
 * Binary frame protocol: type(1B) + length(4B big-endian) + data
 */
#include "voice_client.h"

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "voice_client";

#define MAX_FRAME_LEN (256 * 1024)

esp_err_t voice_client_connect(voice_client_t *client, const char *host, uint16_t port)
{
    if (!client) return ESP_ERR_INVALID_ARG;

    client->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client->sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &dest_addr.sin_addr);

    ESP_LOGI(TAG, "Connecting to %s:%d ...", host, port);

    int err = connect(client->sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Connect failed: errno %d", errno);
        close(client->sock);
        client->sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connected to %s:%d", host, port);
    return ESP_OK;
}

void voice_client_disconnect(voice_client_t *client)
{
    if (client && client->sock >= 0) {
        close(client->sock);
        client->sock = -1;
        ESP_LOGI(TAG, "Disconnected");
    }
}

static esp_err_t send_all(int sock, const uint8_t *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "Send failed: errno %d", errno);
            return ESP_FAIL;
        }
        sent += n;
    }
    return ESP_OK;
}

esp_err_t voice_send_frame(voice_client_t *client, uint8_t type, const uint8_t *data, size_t len)
{
    if (!client || client->sock < 0) return ESP_ERR_INVALID_STATE;

    // Header: type(1B) + length(4B big-endian)
    uint8_t header[5];
    header[0] = type;
    header[1] = (len >> 24) & 0xFF;
    header[2] = (len >> 16) & 0xFF;
    header[3] = (len >> 8) & 0xFF;
    header[4] = len & 0xFF;

    esp_err_t ret = send_all(client->sock, header, 5);
    if (ret != ESP_OK) return ret;

    if (len > 0 && data) {
        ret = send_all(client->sock, data, len);
    }
    return ret;
}

static esp_err_t recv_all(int sock, uint8_t *buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        int n = recv(sock, buf + received, len - received, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "Recv failed: errno %d", errno);
            return ESP_FAIL;
        }
        received += n;
    }
    return ESP_OK;
}

esp_err_t voice_recv_frame(voice_client_t *client, uint8_t *type, uint8_t **data, size_t *len)
{
    if (!client || client->sock < 0) return ESP_ERR_INVALID_STATE;

    uint8_t header[5];
    esp_err_t ret = recv_all(client->sock, header, 5);
    if (ret != ESP_OK) return ret;

    *type = header[0];
    uint32_t frame_len = ((uint32_t)header[1] << 24) |
                         ((uint32_t)header[2] << 16) |
                         ((uint32_t)header[3] << 8) |
                         (uint32_t)header[4];

    if (frame_len > MAX_FRAME_LEN) {
        ESP_LOGE(TAG, "Frame too large: %lu", frame_len);
        return ESP_ERR_INVALID_SIZE;
    }

    *len = frame_len;
    if (frame_len == 0) {
        *data = NULL;
        return ESP_OK;
    }

    *data = heap_caps_malloc(frame_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*data) {
        *data = malloc(frame_len);
    }
    if (!*data) {
        ESP_LOGE(TAG, "Frame alloc failed: %lu bytes", frame_len);
        return ESP_ERR_NO_MEM;
    }

    ret = recv_all(client->sock, *data, frame_len);
    if (ret != ESP_OK) {
        free(*data);
        *data = NULL;
        *len = 0;
    }
    return ret;
}
