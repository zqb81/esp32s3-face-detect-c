# Web 服务说明

`web/` 目录是当前项目的 Python Web 服务，负责把 ESP32-S3 上报的图像与检测结果整理成可浏览、可查询、可被微信小程序复用的接口。

## 当前能力

- 接收 ESP32 上传的最新 JPEG 帧
- 订阅 MQTT 检测结果与人脸裁剪图
- 把检测记录和抓拍图保存到 SQLite
- 提供浏览器看板页面
- 提供原始 MJPEG 流与服务端叠框 MJPEG 流
- 提供给微信小程序使用的 JSON 接口

## 目录说明

- `app.py`：统一后的 Web 服务主入口
- `templates/index.html`：浏览器看板页面
- `requirements.txt`：Python 依赖
- `start.sh`：快速启动脚本
- `video_relay.py`：历史兼容入口，当前已废弃
- `face_detect.db`：运行后自动生成的 SQLite 数据库

## 运行环境

建议环境：

- Python 3.10 及以上
- 可访问 MQTT Broker
- 可写当前目录，用于创建 `face_detect.db`

安装依赖：

```bash
cd web
python -m pip install -r requirements.txt
```

启动服务：

```bash
python app.py
```

或者直接执行：

```bash
./start.sh
```

默认监听地址：

- Host：`0.0.0.0`
- Port：`8082`

## 数据流

当前 `web/app.py` 的数据流如下：

1. ESP32 通过 `POST /upload` 上传最新 JPEG
2. Web 服务通过 MQTT 订阅检测结果与人脸抓拍
3. 服务把检测历史、人脸抓拍写入 SQLite
4. 浏览器看板与微信小程序通过 HTTP 接口读取数据

## MQTT 配置

当前代码中的默认配置：

- Broker：`127.0.0.1`
- Port：`1883`
- 检测主题：`esp32/face_detect`
- 抓拍主题：`esp32/face_detect/crop`

如果部署环境变化，请修改 `app.py` 里的以下常量：

- `MQTT_BROKER`
- `MQTT_PORT`
- `FACE_TOPIC`
- `CROP_TOPIC`

## 数据库说明

数据库文件默认位于：

- `web/face_detect.db`

当前会自动维护以下三张表：

- `detections`：检测记录
- `faces`：检测框详情
- `face_images`：人脸抓拍 JPEG

服务首次启动时会自动建表，无需手工初始化。

## 接口说明

### 页面与视频流

- `GET /`
  - 浏览器看板页面
- `GET /stream`
  - 原始 MJPEG 视频流
- `GET /stream_boxed`
  - 服务端叠框后的 MJPEG 视频流

### 上传接口

- `POST /upload`
  - 由 ESP32 上传最新 JPEG
  - 请求头需要是 `Content-Type: image/jpeg`

### JSON 接口

- `GET /api/stats`
  - 返回统计信息
  - 典型字段：`total_detections`、`total_faces`、`devices_online`、`last_detection`
- `GET /api/latest_detection`
  - 返回最近一次检测结果
- `GET /api/detections?limit=20`
  - 返回最近检测记录
- `GET /api/face_images?limit=20`
  - 返回最近抓拍列表
- `GET /api/face_image/<id>`
  - 返回指定抓拍图片 JPEG

## 与微信小程序的关系

当前仓库里的微信小程序前端会直接复用这些接口：

- `/api/stats`
- `/api/latest_detection`
- `/api/detections`
- `/api/face_images`
- `/api/face_image/<id>`

如果小程序真机调试或正式上线，请优先通过 Nginx 反向代理暴露 HTTPS 域名，不要直接使用 IP 和裸 `http://` 地址。

## 常见问题

### 1. 页面能打开，但没有实时画面

通常是因为 ESP32 还没有向 `/upload` 上传最新 JPEG，或者上传间隔太久导致帧已过期。

### 2. 浏览器看板有数据，但小程序没图片

通常是因为小程序侧没有配置 HTTPS 合法域名，或者图片域名没有加入 `downloadFile` 合法域名。

### 3. `video_relay.py` 还能不能用

不能作为主入口使用。这个文件只保留兼容提示，当前应使用：

```bash
python app.py
```

## 部署建议

如果要给外部访问，建议：

1. 用 Nginx 反代到 `127.0.0.1:8082`
2. 给域名配置 HTTPS 证书
3. 小程序使用单独子域名，例如 `api.example.com`
4. MQTT Broker 与 Web 服务尽量放在同一台服务器或同一内网
