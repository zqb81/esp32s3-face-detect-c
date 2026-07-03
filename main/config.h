/**
 * Global configuration for the ESP32-S3 face detect app.
 *
 * 敏感配置（WiFi/MQTT/服务器地址）放在 config_private.h 中，
 * 该文件已被 .gitignore 排除。首次使用请复制 config_private.h.example
 * 为 config_private.h 并填入实际值。
 */

#ifndef CONFIG_H
#define CONFIG_H

// 导入私有配置（WiFi、MQTT、服务器地址等）
#include "config_private.h"

#define WIFI_MAX_RETRY  15

// MQTT
#define MQTT_PORT       1883
#define MQTT_TOPIC      "esp32/face_detect"
#define MQTT_TOPIC_CROP "esp32/face_detect/crop"
#define CLIENT_ID       "esp32s3_face_c"

// HTTP stream and upload
#define HTTP_STREAM_PORT           81
#define HTTP_UPLOAD_MIN_INTERVAL_MS 200
#define HTTP_UPLOAD_TIMEOUT_MS      2000
#define HTTP_UPLOAD_BUFFER_SIZE     (100 * 1024)
#define HTTP_UPLOAD_TASK_STACK_SIZE 4096
#define HTTP_UPLOAD_TASK_PRIORITY   4

// Camera (OV5640)
#define CAM_PIN_D0      11
#define CAM_PIN_D1      9
#define CAM_PIN_D2      8
#define CAM_PIN_D3      10
#define CAM_PIN_D4      12
#define CAM_PIN_D5      18
#define CAM_PIN_D6      17
#define CAM_PIN_D7      16
#define CAM_PIN_XCLK    15
#define CAM_PIN_PCLK    13
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_SDA     4
#define CAM_PIN_SCL     5

#define CAM_XCLK_FREQ   20000000
#define CAM_W           320
#define CAM_H           240

// TFT (ST7735 128x160)
#define TFT_SPI_HOST    SPI2_HOST
#define TFT_PIN_SCK     14
#define TFT_PIN_MOSI    21
#define TFT_PIN_CS      2
#define TFT_PIN_DC      1
#define TFT_PIN_RST     3
#define TFT_PIN_BL      47
#define TFT_SPI_FREQ    40000000

#define TFT_WIDTH       128
#define TFT_HEIGHT      160

// Face detect
#define FACE_DETECT_INTERVAL_FRAMES 5
#define FACE_DETECT_INTERVAL_MS     200
#define FACE_CROP_SIZE              64
#define FACE_CROP_INTERVAL_MS       1000
#define FACE_CROP_TASK_STACK_SIZE   6144
#define FACE_CROP_TASK_PRIORITY     4
#define FACE_SCORE_THRESHOLD        0.6f
#define FACE_BOX_SCALE              1.3f

// Voice I2S
#define I2S_MIC_SCK     39
#define I2S_MIC_WS      40
#define I2S_MIC_SD      41
#define I2S_SPK_BCLK    42
#define I2S_SPK_LRC     43
#define I2S_SPK_DIN     44
#define AUDIO_SAMPLE_RATE  16000
#define AUDIO_BITS         16
#define AUDIO_CHUNK_SIZE   4096
#define AUDIO_BUF_SIZE     (AUDIO_SAMPLE_RATE * 2 * 10)  // 10s max recording

// Voice button
#define VOICE_BTN_GPIO  45

// Voice TCP server (VOICE_SERVER_HOST 定义在 config_private.h)
#define VOICE_SERVER_PORT   9000
#define VOICE_TCP_TIMEOUT_S 30
#define VOICE_TTS_TIMEOUT_S 60

// Voice task
#define VOICE_TASK_STACK_SIZE   8192
#define VOICE_TASK_PRIORITY     3

// Device control GPIO
// ESP32-S3 with octal PSRAM: GPIO 22-25 don't exist, GPIO 26 = PSRAM CS,
// GPIO 27-32 = flash SPI, GPIO 33-37 = octal PSRAM data lines.
// Free GPIOs: 0, 38, 46, 48 only.
#define DEVICE_RGB_PIN      38   // WS2812 RGB strip
#define DEVICE_RGB_NUM      5    // Number of RGB LEDs
#define DEVICE_BUZZER_PIN   48   // Buzzer PWM

// Disabled — not enough free GPIO on this board
#define DEVICE_MOTOR_ENABLED  0
#define DEVICE_LED_ENABLED    0
#define DEVICE_DHT_ENABLED    0
#define DEVICE_LIGHT_ENABLED  0

#define MOTOR_FREQ          50
#define MOTOR_DUTY          512
#define MOTOR_TIMEOUT_MS    60000
#define BUZZER_FREQ         1000
#define DHT_TYPE            22

// Performance logging
#define PERF_LOG_INTERVAL_MS        2000

// Tasks
#define TASK_STACK_SIZE     8192
#define TASK_PRIORITY       5

#endif // CONFIG_H
