idf_component_register(
    SRCS "main.c" "camera.c" "display.c" "face_detect.c" "mqtt_comm.c" "http_stream.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_timer
        esp_http_server
        driver
    # esp32-camera, esp-dl 通过 idf_component.yml 管理
    # MQTT 手动添加: idf.py add-dependency "espressif/espressif__mqtt"
)
