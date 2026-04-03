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
    ├── esp-dl SCRFD 人脸检测
    ├── TFT 显示 (下采样 + 人脸框)
    ├── MQTT 上报检测结果 + 人脸裁剪
    └── HTTP MJPEG 视频流 (端口 81)
```

## 模块

| 文件 | 功能 |
|------|------|
| `main.c` | 入口 + WiFi + 主循环 |
| `camera.c/h` | OV5640 摄像头驱动 (esp32-camera) |
| `display.c/h` | ST7735 TFT SPI 驱动 |
| `face_detect.c/h` | esp-dl SCRFD 人脸检测 |
| `mqtt_comm.c/h` | MQTT 检测结果 + 人脸上传 |
| `http_stream.c/h` | HTTP MJPEG 视频流 |
| `config.h` | 全局引脚和参数配置 |

## 构建

```bash
# 设置 ESP-IDF 环境
. $HOME/esp/esp-idf/export.sh

# 编译
idf.py set-target esp32s3
idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash monitor
```

## 依赖

- ESP-IDF 5.x+
- [esp32-camera](https://github.com/espressif/esp32-camera)
- [esp-dl](https://github.com/espressif/esp-dl)

### 安装组件

```bash
idf.py add-dependency "espressif/esp32-camera"
idf.py add-dependency "espressif/esp-dl"
```

## 功能

- [x] 摄像头采集 QVGA RGB565
- [x] TFT ST7735 SPI 显示
- [x] WiFi STA 连接
- [x] MQTT 检测结果上报
- [x] MQTT 人脸裁剪上传
- [x] HTTP MJPEG 视频流
- [ ] esp-dl SCRFD 人脸检测（待接入模型）
- [ ] JPEG 硬件编码
- [ ] NTP 时间同步

## 对比 Python 版本

| 特性 | Python (v1.9) | C (本项目) |
|------|--------------|-----------|
| 帧率 | ~4.5 FPS | 预计 15+ FPS |
| 内存占用 | 高 (MicroPython) | 低 |
| HTTP 视频流 | ❌ | ✅ |
| 人脸检测 | espdl 模块 | esp-dl C API |
| 开发效率 | 快 | 中 |
| 部署 | 上传 .py | 编译烧录 |

## 引脚

见 `config.h`，与 Python 版本完全一致。
