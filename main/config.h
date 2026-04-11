/**
 * Global configuration for the ESP32-S3 face detect app.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Wi-Fi
#define WIFI_SSID       "APP"
#define WIFI_PASS       "123456789"
#define WIFI_MAX_RETRY  15

// MQTT
#define MQTT_BROKER     "101.33.209.65"
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

// Performance logging
#define PERF_LOG_INTERVAL_MS        2000

// Tasks
#define TASK_STACK_SIZE     8192
#define TASK_PRIORITY       5

#endif // CONFIG_H
