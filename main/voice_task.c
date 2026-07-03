/**
 * voice_task.c — Voice FreeRTOS task
 * Button press → record audio → TCP to server → receive TTS → play
 */
#include "voice_task.h"

#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "config.h"
#include "voice_i2s.h"
#include "voice_client.h"
#include "device_ctrl.h"

static const char *TAG = "voice_task";

static void execute_action(cJSON *cmd)
{
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "action"));
    if (!action) return;

    if (strcmp(action, "fan") == 0 || strcmp(action, "motor") == 0) {
        cJSON *state = cJSON_GetObjectItem(cmd, "state");
        if (state && strcmp(cJSON_GetStringValue(state), "on") == 0) {
            int speed = MOTOR_DUTY;
            cJSON *spd = cJSON_GetObjectItem(cmd, "speed");
            if (spd) speed = spd->valueint;
            device_motor_on(speed);
        } else {
            device_motor_off();
        }
    } else if (strcmp(action, "led") == 0) {
        cJSON *state = cJSON_GetObjectItem(cmd, "state");
        if (state && strcmp(cJSON_GetStringValue(state), "on") == 0) {
            device_led_on();
        } else {
            device_led_off();
        }
    } else if (strcmp(action, "rgb") == 0) {
        cJSON *color = cJSON_GetObjectItem(cmd, "color");
        if (color) {
            device_rgb_color(cJSON_GetStringValue(color));
        } else {
            int r = 0, g = 0, b = 0;
            cJSON *jr = cJSON_GetObjectItem(cmd, "r");
            cJSON *jg = cJSON_GetObjectItem(cmd, "g");
            cJSON *jb = cJSON_GetObjectItem(cmd, "b");
            if (jr) r = jr->valueint;
            if (jg) g = jg->valueint;
            if (jb) b = jb->valueint;
            device_rgb_set(r, g, b);
        }
    } else if (strcmp(action, "buzzer") == 0) {
        cJSON *state = cJSON_GetObjectItem(cmd, "state");
        if (state && strcmp(cJSON_GetStringValue(state), "on") == 0) {
            int times = 3;
            cJSON *t = cJSON_GetObjectItem(cmd, "times");
            if (t) times = t->valueint;
            device_buzzer_beep(times);
        } else {
            device_buzzer_off();
        }
    } else {
        ESP_LOGI(TAG, "Unknown action: %s", action);
    }
}

static volatile bool s_btn_pressed = false;
static volatile bool s_btn_released = false;
static int64_t s_btn_press_time = 0;

static void IRAM_ATTR btn_isr(void *arg)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (gpio_get_level(VOICE_BTN_GPIO) == 0) {
        s_btn_pressed = true;
        s_btn_released = false;
        s_btn_press_time = now;
    } else {
        s_btn_pressed = false;
        int64_t duration = now - s_btn_press_time;
        if (duration >= 100) {
            s_btn_released = true;
        }
    }
}

static void init_button(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VOICE_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(VOICE_BTN_GPIO, btn_isr, NULL);
}

static esp_err_t record_and_send(voice_client_t *client)
{
    esp_err_t ret;

    ret = voice_mic_init();
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Recording... release button to stop");

    // Send start_recording command
    cJSON *start_cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(start_cmd, "action", "start_recording");
    char *start_json = cJSON_PrintUnformatted(start_cmd);
    ret = voice_send_frame(client, MSG_CMD, (uint8_t *)start_json, strlen(start_json));
    free(start_json);
    cJSON_Delete(start_cmd);
    if (ret != ESP_OK) {
        voice_mic_deinit();
        return ret;
    }

    // Record audio and stream
    uint8_t *audio_buf = heap_caps_malloc(AUDIO_CHUNK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_buf) {
        audio_buf = malloc(AUDIO_CHUNK_SIZE);
    }
    if (!audio_buf) {
        ESP_LOGE(TAG, "Audio buffer alloc failed");
        voice_mic_deinit();
        return ESP_ERR_NO_MEM;
    }

    size_t total_bytes = 0;
    size_t max_bytes = AUDIO_SAMPLE_RATE * 2 * 10;  // 10s max
    s_btn_released = false;

    while (!s_btn_released && total_bytes < max_bytes) {
        size_t bytes_read = 0;
        ret = voice_mic_read(audio_buf, AUDIO_CHUNK_SIZE, &bytes_read, 100);
        if (ret == ESP_OK && bytes_read > 0) {
            voice_send_frame(client, MSG_AUDIO, audio_buf, bytes_read);
            total_bytes += bytes_read;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    free(audio_buf);
    voice_mic_deinit();

    ESP_LOGI(TAG, "Recording done: %zu bytes", total_bytes);

    // Send stop_recording command
    cJSON *stop_cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(stop_cmd, "action", "stop_recording");
    char *stop_json = cJSON_PrintUnformatted(stop_cmd);
    ret = voice_send_frame(client, MSG_CMD, (uint8_t *)stop_json, strlen(stop_json));
    free(stop_json);
    cJSON_Delete(stop_cmd);

    return ret;
}

static esp_err_t receive_and_play(voice_client_t *client)
{
    esp_err_t ret;

    ret = voice_spk_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Receiving TTS...");

    uint8_t *play_buf = heap_caps_malloc(AUDIO_CHUNK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!play_buf) {
        play_buf = malloc(AUDIO_CHUNK_SIZE);
    }

    while (1) {
        uint8_t frame_type = 0;
        uint8_t *frame_data = NULL;
        size_t frame_len = 0;

        ret = voice_recv_frame(client, &frame_type, &frame_data, &frame_len);
        if (ret != ESP_OK) break;

        if (frame_type == MSG_DONE) {
            if (frame_data) free(frame_data);
            break;
        }

        if (frame_type == MSG_AUDIO && frame_data && frame_len > 0) {
            size_t written = 0;
            voice_spk_write(frame_data, frame_len, &written, 1000);
        }

        if (frame_type == MSG_CMD && frame_data && frame_len > 0) {
            cJSON *cmd = cJSON_ParseWithLength((char *)frame_data, frame_len);
            if (cmd) {
                ESP_LOGI(TAG, "Command: %s", frame_data);
                execute_action(cmd);
                cJSON_Delete(cmd);
            }
        }

        if (frame_data) free(frame_data);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (play_buf) free(play_buf);
    voice_spk_deinit();

    ESP_LOGI(TAG, "Playback done");
    return ESP_OK;
}

static void voice_task(void *arg)
{
    ESP_LOGI(TAG, "Voice task started on core %d", xPortGetCoreID());
    init_button();

    while (1) {
        if (s_btn_released) {
            s_btn_released = false;

            ESP_LOGI(TAG, "Button pressed - starting voice interaction");

            voice_client_t client = {.sock = -1};
            esp_err_t ret = voice_client_connect(&client, VOICE_SERVER_HOST, VOICE_SERVER_PORT);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to connect to voice server");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            ret = record_and_send(&client);
            if (ret == ESP_OK) {
                receive_and_play(&client);
            }

            voice_client_disconnect(&client);
            ESP_LOGI(TAG, "Voice interaction complete");
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t voice_task_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        voice_task,
        "voice_task",
        VOICE_TASK_STACK_SIZE,
        NULL,
        VOICE_TASK_PRIORITY,
        NULL,
        0  // Core 0 (capture_task is also on Core 0 but mostly waiting)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create voice task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Voice task created");
    return ESP_OK;
}
