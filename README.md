# ESP32-S3 人脸检测 (C 版本)

基于 ESP-IDF + esp-dl 的端侧实时人脸检测，C 语言实现。

## 硬件

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-S3-WROOM-1 N16R8 |
| 摄像头 | OV5640 |
| 屏幕 | ST7735 1.8寸 TFT (128×160) |

## 架构

```
摄像头 (QVGA RGB565)
    ├── esp-dl SCRFD 人脸检测（模型位于 SPIFFS 分区 /model）
    ├── TFT 显示 (下采样 + 人脸框)
    ├── MQTT 上报检测结果 + 人脸裁剪
    └── HTTP MJPEG 视频流 (端口 81)
```

## 模块

| 文件 | 功能 |
|------|------|
| `main.c` | 入口 + WiFi + 主循环 + 挂载 SPIFFS:model 分区 |
| `camera.c/h` | OV5640 摄像头驱动 (esp32-camera) |
| `display.c/h` | ST7735 TFT SPI 驱动 |
| `face_detect.cpp/h` | esp-dl SCRFD 加载/预处理/后处理/推理 |
| `mqtt_comm.c/h` | MQTT 检测结果 + 人脸上传 |
| `http_stream.c/h` | HTTP MJPEG 视频流 |
| `config.h` | 全局引脚和参数配置 |

## 构建与烧录

```bash
# 1) 设置 ESP-IDF 环境
. $HOME/esp/esp-idf/export.sh

# 2) 选择目标 & 构建
idf.py set-target esp32s3
idf.py build

# 3) 烧录 + 串口监视
idf.py -p /dev/ttyUSB0 flash monitor
```

注意：顶层 CMake 已启用

- 将 `main/models/` 打包为名为 `model` 的 SPIFFS 分区镜像（见 partitions.csv）
- 烧录时固件会自动连带烧写该分区

## 依赖

- ESP-IDF 5.x+
- [esp32-camera](https://github.com/espressif/esp32-camera)
- [esp-dl](https://github.com/espressif/esp-dl)

通过 `main/idf_component.yml` 自动拉取依赖；`main/CMakeLists.txt` 已添加 `spiffs` 组件。

## 功能

- [x] 摄像头采集 QVGA RGB565
- [x] TFT ST7735 SPI 显示
- [x] WiFi STA 连接
- [x] MQTT 检测结果上报
- [x] MQTT 人脸裁剪上传
- [x] HTTP MJPEG 视频流（默认端口 81）
- [x] esp-dl SCRFD 人脸检测（从 `/model` 加载 `.espdl`）
- [ ] JPEG 硬件编码
- [ ] NTP 时间同步

## 📥 模型获取与放置（必读）

- 模型文件名：`scrfd_face_detect.espdl`
- 放置路径：`main/models/scrfd_face_detect.espdl`
- 运行时读取路径：`/model/scrfd_face_detect.espdl`（SPIFFS 分区挂载在 `/model`）

获取方式详见 `main/models/README.md`，常用路径：
- 方式1：从 esp-dl 示例获取预编译 `.espdl`
- 方式2：下载 SCRFD onnx（如 `scrfd_500m_bnkps.onnx`）并使用 `esp-dl` 转换：
  ```bash
  python3 -m pip install esp-dl
  python3 -m esp_dl.convert --model scrfd_500m_bnkps.onnx --output scrfd_face_detect.espdl
  ```
- 方式3：使用社区已转换的 `.espdl`（注意尺寸与兼容性）

放入 `main/models/` 后执行 `idf.py build && idf.py flash` 即可随固件一起烧录到 `model` 分区。

## 运行与日志

- 串口启动日志可见：
  - `模型分区挂载成功: /model ...`
  - `SCRFD 模型加载成功: /model/scrfd_face_detect.espdl`
- 视频流：`http://<设备IP>:81`
- MQTT：`MQTT_BROKER` 等参数见 `config.h`

## 对比 Python 版本

| 特性 | Python (v1.9) | C (本项目) |
|------|--------------|-----------|
| 帧率 | ~4.5 FPS | 预计 15+ FPS |
| 内存占用 | 高 (MicroPython) | 低 |
| HTTP 视频流 | ❌ | ✅ |
| 人脸检测 | espdl 模块 | esp-dl C API |
| 部署 | 上传 .py | 编译烧录 + SPIFFS 模型 |

## 引脚

见 `config.h`，与 Python 版本一致。
