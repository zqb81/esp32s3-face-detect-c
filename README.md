# ESP32-S3 人脸检测项目

这是一个基于 ESP-IDF 和 `esp-dl` 的 ESP32-S3 实时人脸检测项目，包含：

- 设备端固件（C / ESP-IDF 5.4.x）
- TFT 本地预览与人脸框显示
- MQTT 检测结果与人脸裁剪上传
- 语音交互（I2S 麦克风 + 扬声器 + TCP 语音服务）
- 设备控制（蜂鸣器 + WS2812 RGB 灯带）
- Web 端上传服务、实时画面和检测看板
- 微信小程序结果看板（含设备控制 + 语音助手）

当前项目已经打通以下链路：

- OV5640 采集 JPEG 图像
- ESP32-S3 解码为 RGB565
- `esp-dl` 两级人脸检测模型推理
- ST7735 TFT 显示检测框（手动 CS 控制）
- MQTT 上报检测框和人脸裁剪图
- HTTP 上传最新 JPEG 到 `web/` 服务
- Web 端展示原始流、叠框流、检测记录和最近抓拍
- 微信小程序复用 Web JSON 接口展示统计、记录和抓拍
- 语音交互：INMP441 麦克风录音 → TCP 发送到云端 ASR/LLM/TTS → MAX98357A 扬声器播放
- 设备控制：蜂鸣器 (LEDC PWM) + WS2812 RGB 灯带 (RMT)
- Web 和小程序端的语音助手 UI + 设备控制面板

## 1. 项目状态

当前仓库已经完成并验证过的关键功能：

- 人脸模型可以正常加载
- 本地 TFT 检测框已经恢复显示
- Web 前后端代码统一放在 `web/` 目录
- 原生微信小程序前端已创建在 `miniapp/` 目录
- 云端叠框所需的 `frame`、`img_w`、`img_h` 已加入 MQTT 消息
- Web 端”最近检测时间”显示成 `1970-01-01 08:xx:xx` 的问题已修复
- Web 看板已支持在 `socket.io` 客户端不可用时自动降级到 5 秒轮询
- 语音交互模块已集成（I2S 录音/播放 + TCP 语音服务）
- 设备控制模块已集成（蜂鸣器 GPIO48 + WS2812 RGB GPIO38）
- Web 和小程序设备控制 UI 已同步更新（仅蜂鸣器 + RGB，无风扇/LED/传感器）
- 内存优化：heap/FreeRTOS/ringbuf 函数移至 Flash，mbedTLS 分配移至 PSRAM
- ST7735 显示修复：完整初始化序列 + 手动 CS 控制
- 设备端已加入帧率优化版本：
  - 远端 HTTP 上传改为异步上传线程
  - 人脸裁剪图 MQTT 发布改为异步 worker
  - 主处理链路不再被同步上传阻塞
  - 新增解码、缩放、检测、裁剪提交、刷屏、总耗时统计
  - 已修复一次裁剪图 Base64 编码长度探测回归，避免 `esp32/face_detect/crop` 长时间静默

## 2. 硬件信息

| 部件 | 型号 | 引脚 |
|------|------|------|
| MCU | ESP32-S3-WROOM-1 N16R8 | — |
| 摄像头 | OV5640 | D0-D7: 11,9,8,10,12,18,17,16; XCLK:15 PCLK:13 VSYNC:6 HREF:7 SDA:4 SCL:5 |
| 显示屏 | ST7735 1.8寸 128×160 | SCK:14 MOSI:21 CS:2 DC:1 RST:3 BL:47 |
| 麦克风 | INMP441 (I2S) | SCK:39 WS:40 SD:41 |
| 扬声器 | MAX98357A (I2S) | BCLK:42 LRC:43 DIN:44 |
| 蜂鸣器 | 有源蜂鸣器 (LEDC PWM) | GPIO 48 |
| RGB灯带 | WS2812 ×5 (RMT) | GPIO 38 |
| 按键 | 语音触发按钮 | GPIO 45 |

### GPIO 约束

ESP32-S3 搭配 Octal PSRAM 时，以下引脚不可用：

- GPIO 22-25：不存在
- GPIO 26：PSRAM CS
- GPIO 27-32：Flash SPI
- GPIO 33-37：Octal PSRAM 数据线

可用于外设的剩余 GPIO：0, 38, 46, 48

## 3. 系统架构

```text
OV5640 (QVGA JPEG)
  -> Core 0 拍照并复制到缓冲池
  -> Core 1 解码 JPEG 为 RGB565
  -> esp-dl 人脸检测（MSR + MNP）
  -> TFT 本地叠框显示
  -> MQTT 上传检测框 / 人脸裁剪
  -> HTTP 上传最新 JPEG 到 Web 服务

语音交互 (按键触发)
  -> INMP441 I2S 录音
  -> TCP 发送到云端语音服务 (<server-ip>:9000)
  -> ASR -> LLM -> TTS
  -> MAX98357A I2S 播放回复
  -> 解析动作指令控制蜂鸣器/RGB

设备控制 (MQTT cmd)
  -> esp32/iot/cmd/buzzer  -> LEDC PWM (GPIO48)
  -> esp32/iot/cmd/rgb     -> WS2812 RMT (GPIO38)
  -> esp32/iot/cmd/facedetect -> 软件开关
```

当前帧率优化后的核心处理思路：

```text
process_task
  -> JPEG 解码
  -> 更新本地 MJPEG 缓冲
  -> 投递最新帧给异步上传线程
  -> downsample 到 TFT
  -> 条件触发人脸检测
  -> 提交最佳人脸给异步裁剪 worker
  -> 叠框
  -> 刷屏

upload_task
  -> 只保留最近一帧
  -> 独立执行 HTTP 上传
  -> 失败不阻塞主处理链路

face_crop_task
  -> 只保留最近一张待发送裁剪图
  -> 独立执行 Base64 编码
  -> 独立执行 MQTT 发布
  -> 发布失败写日志，避免静默丢失
```

## 4. 目录说明

### 设备端主要文件

| 文件 | 作用 |
|------|------|
| `main/main.c` | 程序入口、Wi-Fi、任务调度、图像处理主链路 |
| `main/camera.c` | OV5640 初始化、采图、JPEG 解码 |
| `main/display.c` | ST7735 显示驱动（手动 CS 控制） |
| `main/face_detect.cpp` | `esp-dl` 人脸检测封装 |
| `main/http_stream.c` | 本地 MJPEG 流和异步 HTTP 上传 |
| `main/mqtt_comm.c` | MQTT 检测结果与裁剪图上传 |
| `main/voice_i2s.c` | I2S 麦克风/扬声器驱动 |
| `main/voice_client.c` | TCP 语音客户端（录音→发送→接收 TTS） |
| `main/voice_task.c` | 语音任务（按键触发交互 + 动作执行） |
| `main/device_ctrl.c` | 设备控制（蜂鸣器 LEDC + WS2812 RMT） |
| `main/config.h` | 引脚、网络、运行参数 |

### Web 端主要文件

| 文件 | 作用 |
|------|------|
| `web/app.py` | Web 服务主程序，负责上传接口、MQTT 接收、数据库、看板页面 |
| `web/README.md` | Web 服务单独说明文档 |
| `web/templates/index.html` | Web 看板页面 |
| `web/start.sh` | Web 服务启动脚本 |
| `web/requirements.txt` | Web 依赖 |
| `web/video_relay.py` | 兼容保留文件，当前部署优先使用 `web/app.py` |

### 微信小程序主要文件

| 文件 | 作用 |
|------|------|
| `miniapp/README.md` | 微信小程序使用说明 |
| `miniapp/app.json` | 小程序全局页面配置 |
| `miniapp/utils/api.js` | 小程序接口封装与 `BASE_URL` |
| `miniapp/pages/home/home.*` | 首页看板 |
| `miniapp/pages/detections/detections.*` | 检测记录页 |
| `miniapp/pages/faces/faces.*` | 人脸抓拍页 |
| `miniapp/pages/control/control.*` | 设备控制页（蜂鸣器/RGB/语音助手） |

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

注意：

- 模型不是放在 SPIFFS 文件系统里
- `esp-dl` 是直接从原始 Flash 分区加载模型

## 6. 已完成的重要修复

### 6.1 人脸框不显示

之前的现象：

- TFT 没有人脸框
- Web 端也没有人脸框
- 但模型其实已经加载成功

根因有两个：

1. JPEG 解码后的 RGB565 字节序和模型输入格式不一致
2. TFT 之前直接申请整块 DMA 缓冲，存在分配失败风险

对应修复：

- `main/face_detect.cpp`
  - 将模型输入像素类型从 `RGB565LE` 改为 `RGB565BE`
- `main/main.c`
  - 将 TFT 全帧缓冲改到 PSRAM
- `main/display.c`
  - 使用小块 DMA bounce buffer 分段刷屏
- `main/main.c`
  - 缓存最近一次检测结果，让跳帧期间 TFT 框不立刻消失

### 6.2 Web 最近检测时间显示成 1970

根因：

- 设备端 MQTT 上报的 `ts` 使用的是 `esp_timer_get_time()/1000000`
- 这个值表示设备上电后的运行秒数，不是真实 Unix 时间戳
- Web 端直接 `fromtimestamp()` 后会显示成 `1970-01-01 08:xx:xx`

修复：

- `web/app.py` 已增加时间戳归一化逻辑
- 如果收到的时间戳明显不合理，会自动改用服务端当前时间
- 同时兼容秒级和毫秒级 Unix 时间戳

说明：

- 修复后，新写入的记录时间正常
- 历史数据库中已经保存的错误时间不会自动纠正

### 6.3 帧率优化

这次帧率优化的目标是优先提升整体流畅度，不通过降低分辨率或图像质量换速度。

主要改动：

- `main/http_stream.c`
  - 新增异步上传线程
  - 采用 latest-only 策略，只保留最近一帧待上传 JPEG
  - 上传失败不阻塞主处理线程
- `main/main.c`
  - 主处理线程不再同步执行 HTTP 上传
  - 新增阶段耗时统计：
    - JPEG 解码
    - downsample
    - 人脸检测
    - 裁剪提交
    - TFT 刷屏
    - 单帧总耗时
- `main/mqtt_comm.c`
  - 新增人脸裁剪异步 worker
  - 人脸裁剪路径支持单独节流，避免频繁 Base64 编码阻塞主流程
  - 裁剪图 MQTT 发布失败会输出日志，便于定位
- `main/config.h`
  - 新增上传间隔、上传缓冲、检测间隔帧数、人脸裁剪 worker 栈大小/优先级、性能日志周期等配置项

优化后的设计重点：

- 云端上传慢时，不再把主处理链路拖住
- 新帧优先，旧待上传帧允许丢弃
- 本地显示和检测优先级高于远端上传完整性

### 6.4 人脸裁剪 MQTT 回归修复

这次排查到过一个比较隐蔽的回归问题：

- `/upload` 还在持续更新
- 检测结果 topic 仍然有消息
- 但 `esp32/face_detect/crop` 长时间没有新消息
- Web 看板和小程序的“最近人脸裁剪”只能一直显示旧图

根因：

- `main/mqtt_comm.c` 在异步化后，使用 `mbedtls_base64_encode(NULL, 0, ...)` 先探测 Base64 输出长度
- 这个调用本来就会返回 `MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL`，同时把所需长度写回 `b64_len`
- 如果把这个返回值直接当成真正失败处理，裁剪图会在编码前提前退出，导致 MQTT 抓拍主题静默

当前修复：

- 正确接受长度探测阶段的 `MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL`
- 第二次正式编码时使用实际可写缓冲区大小
- MQTT 发布失败时输出警告日志，避免后续再次出现“无新图但无错误”的静默状态

## 7. 构建环境

当前验证环境：

- ESP-IDF 5.4.x
- 芯片目标：`esp32s3`

常规构建：

```bash
idf.py set-target esp32s3
idf.py build
```

本仓库也可使用单独构建目录：

```bash
idf.py -B build-codex build
```

## 8. 烧录

标准命令：

```bash
idf.py -p <串口> flash monitor
```

Windows 下本仓库实际验证可用的形式：

```powershell
idf.py -B build-codex -p COM8 flash
```

说明：

- 烧录应用时会同时烧录模型分区
- 已验证应用分区和模型分区都能成功写入

## 9. Web 服务部署

`web/` 目录就是当前云端服务代码。

Web 服务更详细的说明见：

- `web/README.md`

安装依赖并启动：

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

### 微信小程序前端

微信小程序前端位于：

- `miniapp/`

更详细的说明见：

- `miniapp/README.md`

当前小程序复用以下 Web JSON 接口：

- `/api/stats`
- `/api/latest_detection`
- `/api/detections`
- `/api/face_images`
- `/api/face_image/<id>`
- `/api/voice_chat`
- `/api/device`

当前小程序默认访问地址配置为：

- `https://<your-domain>`

小程序包含 4 个页面：

- **首页**：统计概览、最新检测结果、人脸抓拍预览
- **检测记录**：历史检测列表
- **人脸抓拍**：人脸裁剪图库
- **设备控制**：蜂鸣器开关、RGB 灯带颜色选择、语音助手对话

说明：

- 真机调试和正式上线时，应配置 `request` 与 `downloadFile` 合法域名
- 小程序不直接连接 MQTT，也不直接连接 ESP32
- 设备控制通过 Web 后端的 `/api/device` 转发 MQTT 命令实现

## 10. Web 接口说明

### 页面和视频流

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
- `GET /api/face_image/<id>`
  - 指定人脸抓拍 JPEG
- `GET /api/latest_detection`
  - 最近一次检测结果
- `GET /api/stats`
  - 总检测数、总人脸数、在线设备数、最近检测时间
- `POST /api/voice_chat`
  - 语音助手对话（文本输入 → LLM 回复 + 设备动作）
- `POST /api/device`
  - 设备控制（`action`: buzzer/rgb/face_detect，`state`: on/off/颜色名）

## 11. 看板显示逻辑

当前 Web 看板有两种叠框方式：

### 前端叠框

- 页面从 `/stream` 获取原始视频流
- 优先通过 WebSocket 推送结果更新浏览器中的框
- 如果 `socket.io` 客户端脚本不可用，或者 WebSocket 连接断开，会自动回退到 HTTP 轮询，继续刷新统计、检测记录、人脸裁剪图和最近检测结果
- 用于观察浏览器侧叠框效果

### 服务端叠框

- 页面从 `/stream_boxed` 获取带框视频流
- 服务端将最新 JPEG 和最新检测框合成后输出
- 用于对照浏览器叠框和服务端叠框是否一致

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

说明：

- 当前设备端会按 `FACE_CROP_INTERVAL_MS` 对裁剪图发送节流
- 如果看板视频流和检测记录都在更新，但抓拍图不更新，优先检查设备端是否仍在发布 `esp32/face_detect/crop`
- 此时可以同时核对设备串口日志里的 `crop:` 耗时和服务器侧对该 topic 的订阅结果

## 13. 运行说明

### 设备端上传地址

当前设备端固件里写死的 JPEG 上传地址是：

- `http://<server-ip>:8082/upload`

对应代码位置：

- `main/http_stream.c`

如果后续要把设备端上传地址切到域名或 HTTPS，需要同步修改 `UPLOAD_URL`。

### 对外 HTTPS 访问地址

当前给 Web / 微信小程序使用的对外 HTTPS 访问地址为：

- `https://<your-domain>`

### 本地流地址

设备端本地 MJPEG 服务默认端口：

- `81`

### 帧率统计日志

帧率优化版本会输出以下统计信息：

- `FPS`
- `dec`
  - JPEG 解码平均/最大耗时
- `down`
  - 缩放到 TFT 的平均/最大耗时
- `det`
  - 人脸检测平均/最大耗时
- `draw`
  - TFT 刷屏平均/最大耗时
- `total`
  - 单帧总处理平均/最大耗时
- `UPLOAD`
  - 异步 HTTP 上传平均/最大耗时、成功/失败次数

如果当前日志级别过低，看不到这些统计，可以临时提高日志级别或将统计日志调到 `WARN` 级别观察。

### 当前已知现象

如果仍看到以下日志：

- HTTP 上传超时
- `ESP_ERR_HTTP_CONNECT`
- `ESP_ERR_HTTP_EAGAIN`

通常说明：

- 云端服务未启动
- 服务器不可达
- 网络链路不通

这些问题不会影响本地模型是否运行，但会影响云端画面和记录更新。

另外，最近一次硬件验证中遇到过一次：

- `camera_init failed: ESP_FAIL`

对应日志来自 SCCB/摄像头探测失败，更像是硬件连接、供电或插拔状态问题，不是这次帧率优化代码本身引入的编译错误。重新上电或重新连接摄像头后再验证即可。

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

