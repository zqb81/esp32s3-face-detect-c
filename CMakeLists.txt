# ESP32-S3 人脸检测 C 项目
# ESP-IDF 5.x / esp-dl + SCRFD
cmake_minimum_required(VERSION 3.16)

# 排除不需要的测试组件，避免 unity 构建
set(EXCLUDE_COMPONENTS unity idf_test)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_face_detect)

# 将 main/models 下内容打包到名为 `model` 的 SPIFFS 分区镜像（见 partitions.csv）
# 这样 `scrfd_face_detect.espdl` 会随固件一起烧录到 `model` 分区
spiffs_create_partition_image(model "main/models" FLASH_IN_PROJECT)
