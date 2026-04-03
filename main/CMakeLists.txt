idf_component_register(
    SRCS "main.c" "camera.c" "display.c" "face_detect.c" "mqtt_comm.c" "http_stream.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_timer
        mqtt
        esp_http_server
        esp-dl
        esp32-camera
        driver
)
