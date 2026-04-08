# ESP32-S3 人脸检测项目

这是一个基于 ESP-IDF 和 `esp-dl` 的 ESP32-S3 实时人脸检测项目，包含：

- 设备端固件
- TFT 本地预览与人脸框显示
- MQTT 检测结果与人脸裁剪上传
- Web 端上传服务、实时画面和检测看板

当前项目已经打通以下链路：

- OV5640 采集 JPEG 图像
- ESP32-S3 解码为 RGB565
- `esp-dl` 两级人脸检测模型推理
- ST7735 TFT 显示检测框
- MQTT 上报检测框和人脸裁剪图
- HTTP 上传最新 JPEG 到 `web/` 服务
- Web 端展示原始流、叠框流、检测记录和最近抓拍

## 1. 项目状态

当前仓库已验证通过的关键点：

- 人脸模型能够正常加载
- 本地 TFT 检测框已恢复显示
- Web 端前后端代码已合并在 `web/` 目录
- 云端叠框所需的 `frame`、`img_w`、`img_h` 已加入 MQTT 消息
- Web 端“最近检测时间”错误显示为 `1970-01-01 08:xx:xx` 的问题已修复

已经确认过的一个关键修复：

- 设备端 JPEG 解码后输出的 RGB565 数据，实际应按 `RGB565BE` 传给 `esp-dl`
- 之前代码使用的是 `RGB565LE`，会导致模型运行了但一直检测不到人脸

## 2. 硬件信息

| 部件 | 型号 |
|------|------|
| MCU | ESP32-S3-WROOM-1 N16R8 |
| 摄像头 | OV5640 |
| 显示屏 | ST7735 1.8 寸 TFT，128x160 |

## 3. 系统架构

```text
OV5640 (QVGA JPEG)
  -> Core 0 拍照并复制到缓冲池
  -> Core 1 解码 JPEG 为 RGB565
  -> esp-dl 人脸检测（MSR + MNP）
  -> TFT 本地叠框显示
  -> MQTT 上传检测框 / 人脸裁剪
  -> HTTP 上传最新 JPEG 到 Web 服务
```

## 4. 目录说明

### 设备端主要文件

| 文件 | 作用 |
|------|------|
| `main/main.c` | 程序入口、Wi-Fi、任务调度、整条图像处理链路 |
| `main/camera.c` | OV5640 初始化、采图、JPEG 解码 |
| `main/display.c` | ST7735 显示驱动 |
| `main/face_detect.cpp` | `esp-dl` 人脸检测封装 |
| `main/http_stream.c` | 设备端本地 MJPEG 流与远端上传 |
| `main/mqtt_comm.c` | MQTT 检测结果与裁剪图上传 |
| `main/config.h` | 引脚、网络、运行参数 |

### Web 端主要文件

| 文件 | 作用 |
|------|------|
| `web/app.py` | Web 服务主程序，负责上传接口、MQTT 接收、数据库、看板页面 |
| `web/templates/index.html` | Web 看板页面 |
| `web/start.sh` | Web 服务启动脚本 |
| `web/requirements.txt` | Web 依赖 |
| `web/video_relay.py` | 兼容保留文件，当前部署应优先使用 `web/app.py` |

## 5. 模型说明

当前使用官方 ESP32-S3 可用的人脸检测模型：

- `main/models/face_msr.espdl`
- `main/models/face_mnp.espdl`

构建时，顶层 `CMakeLists.txt` 会调用：

- `managed_components/espressif__esp-dl/fbs_loader/pack_espdl_models.py`

将多个模型打包成一个分区镜像，例如：

- `build-codex/face_models.espdl`

该镜像会烧录到 `partitions.csv` 中定义的 `model` 分区，默认偏移地址：

- `0x310000`

这里要注意：

- 模型不是放在 SPIFFS 文件系统里
- `esp-dl` 是直接从原始 Flash 分区加载模型

## 6. 主要修复记录

### 6.1 人脸框不显示

之前出现过下面这个现象：

- TFT 没有人脸框
- 云端看板也没有人脸框
- 但模型实际上已经加载成功

根因有两个：

1. JPEG 解码输出后的像素格式和模型输入格式不一致
2. TFT 原先直接申请整块 DMA 内存，存在分配失败风险

对应修复如下：

- `main/face_detect.cpp`
  - 将模型输入像素类型从 `RGB565LE` 改为 `RGB565BE`
- `main/main.c`
  - 将 TFT 全帧缓冲改为放在 PSRAM
- `main/display.c`
  - 使用小块 DMA bounce buffer 分段刷屏，避免整帧 DMA 内存申请失败
- `main/main.c`
  - 缓存最近一次检测结果，让跳帧期间 TFT 上的框不会立刻消失

### 6.2 Web 最近检测时间显示成 1970

根因：

- 设备端 MQTT 上报的 `ts` 使用的是 `esp_timer_get_time()/1000000`
- 这个值表示设备上电后的运行秒数，不是真实 Unix 时间戳
- Web 端直接 `fromtimestamp()` 后，就会显示成 `1970-01-01 08:xx:xx`

修复：

- `web/app.py` 已增加时间戳归一化逻辑
- 如果收到的时间戳明显不合理，会自动改用服务端当前时间
- 同时兼容秒级和毫秒级 Unix 时间戳

说明：

- 修复后，新写入的记录时间会正常
- 旧数据库中已经保存的 `1970` 历史记录不会自动纠正

## 7. 构建环境

当前验证环境：

- ESP-IDF 5.4.x
- 芯片目标：`esp32s3`

常规构建流程：

```bash
idf.py set-target esp32s3
idf.py build
```

如果你的工程里已经固定使用单独构建目录，也可以使用：

```bash
idf.py -B build-codex build
```

## 8. 烧录

标准命令：

```bash
idf.py -p <串口> flash monitor
```

Windows 下本仓库实际验证可用的形式类似：

```powershell
idf.py -B build-codex -p COM8 flash
```

说明：

- 烧录应用时会同时烧录打包后的模型分区
- 已经验证过应用分区和模型分区都能成功写入

## 9. Web 服务部署

`web/` 目录就是当前云端服务代码。

进入目录后安装依赖并启动：

```bash
cd web
python3 -m pip install -r requirements.txt
python3 app.py
```

也可以直接运行：

```bash
./start.sh
```

默认端口：

- `8082`

## 10. Web 接口说明

### 页面和流

- `GET /`
  - 看板首页
- `GET /stream`
  - 原始 MJPEG 流
- `GET /stream_boxed`
  - 服务端叠框后的 MJPEG 流

### 数据接口

- `POST /upload`
  - ESP32 上传最新 JPEG
- `GET /api/detections`
  - 最近检测记录
- `GET /api/face_images`
  - 最近人脸裁剪图
- `GET /api/latest_detection`
  - 最近一次检测结果
- `GET /api/stats`
  - 总检测数、总人脸数、在线设备数、最近检测时间

## 11. 看板显示逻辑

当前 Web 看板有两种叠框方式：

### 前端叠框

- 页面从 `/stream` 获取原始视频流
- 同时通过 MQTT 推送结果更新浏览器中的框
- 用于观察浏览器端叠框效果

### 服务端叠框

- 页面从 `/stream_boxed` 获取带框视频流
- 服务端将最新 JPEG 和最新检测框合成后输出
- 用于对照排查“浏览器层叠框”和“服务端层叠框”是否一致

## 12. MQTT 消息内容

### 检测结果消息

当前检测结果消息包含：

- `device`
- `ts`
- `frame`
- `count`
- `img_w`
- `img_h`
- `faces`

其中 `faces` 中每项包含：

- `score`
- `box`

### 人脸裁剪消息

当前人脸裁剪消息包含：

- `device`
- `ts`
- `score`
- `img_w`
- `img_h`
- `img_data`
- `box`

## 13. 运行说明

### 设备端上传地址

当前设备端会把 JPEG 上传到：

- `http://101.33.209.65:8082/upload`

### 本地流地址

设备端本地 MJPEG 服务默认端口：

- `81`

### 当前已知现象

目前人脸检测和本地叠框已经修复完成。

如果仍看到这类日志：

- HTTP 上传超时
- `ESP_ERR_HTTP_CONNECT`
- `ESP_ERR_HTTP_EAGAIN`

通常说明：

- 云端服务未启动
- 服务器不可达
- 网络链路不通

这类问题不会影响本地模型是否运行，但会影响云端画面和记录更新。

## 14. 依赖

### 固件依赖

- ESP-IDF 5.4.x
- `espressif/esp32-camera`
- `espressif/esp-dl`
- `espressif/esp_jpeg`

固件依赖通过 `idf_component.yml` / 组件管理自动解析。

### Web 依赖

- Flask
- Flask-SocketIO
- paho-mqtt
- Pillow

Web 依赖定义在：

- `web/requirements.txt`

## 15. 建议

如果后面准备继续完善，建议优先做这几项：

- 给设备增加真实时间同步，例如 NTP
- Web 端增加历史错误时间数据清理脚本
- 给设备端增加更多运行状态诊断
- 对上传失败做更清晰的重试与状态展示
