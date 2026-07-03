# ESP32-S3 人脸检测 C 项目
# ESP-IDF 5.x / esp-dl + SCRFD
cmake_minimum_required(VERSION 3.16)

# 排除不需要的测试组件，避免 unity 构建
set(EXCLUDE_COMPONENTS unity idf_test)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_ai_security_c)

# 将 main/models 下内容打包到名为 `model` 的 SPIFFS 分区镜像（见 partitions.csv）
# 这样 `scrfd_face_detect.espdl` 会随固件一起烧录到 `model` 分区
set(espdl_dir "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp-dl")
set(pack_model_exe "${espdl_dir}/fbs_loader/pack_espdl_models.py")
set(packed_model "${CMAKE_BINARY_DIR}/face_models.espdl")
set(face_models
    "${CMAKE_SOURCE_DIR}/main/models/face_msr.espdl"
    "${CMAKE_SOURCE_DIR}/main/models/face_mnp.espdl")

add_custom_command(
    OUTPUT "${packed_model}"
    COMMAND python "${pack_model_exe}" --model_path ${face_models} --out_file "${packed_model}"
    DEPENDS ${face_models}
    COMMENT "Packing esp-dl face models"
    VERBATIM)

add_custom_target(face_models_bin ALL DEPENDS "${packed_model}")
add_dependencies(flash face_models_bin)
esptool_py_flash_to_partition(flash "model" "${packed_model}")
