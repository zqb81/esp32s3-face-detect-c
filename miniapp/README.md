# ESP32-S3 微信小程序看板

这是一个基于原生微信小程序实现的轻量前端，用来展示当前仓库 `web/app.py` 暴露出的检测统计、最新检测记录和人脸抓拍数据，同时支持设备控制和语音助手。

## 目录说明

- `app.*`：小程序全局配置与全局样式
- `pages/home/`：首页看板
- `pages/detections/`：检测记录页
- `pages/faces/`：人脸抓拍页
- `pages/control/`：设备控制页（蜂鸣器/RGB灯带/语音助手）
- `utils/api.js`：接口请求封装与 `BASE_URL`
- `utils/format.js`：前端格式化工具

## 如何运行

1. 先启动当前仓库的 Web 服务：

   ```bash
   cd web
   python app.py
   ```

2. 用微信开发者工具打开 `miniapp/` 目录。

3. 如需修改后端地址，编辑 `miniapp/utils/api.js` 里的 `BASE_URL`。

4. 开发调试阶段如果仍然使用 HTTP，请在微信开发者工具里开启"不校验合法域名、web-view（业务域名）、TLS 版本以及 HTTPS 证书"。

## 当前功能

- 首页展示服务连接状态、最近刷新时间、统计卡片、最新检测结果、人脸抓拍预览
- 检测记录页展示最近 20 条检测记录
- 人脸抓拍页展示最近 20 张抓拍图片
- 设备控制页：
  - 蜂鸣器开关（LEDC PWM，GPIO 48）
  - 人脸检测开关
  - RGB 灯带颜色选择（WS2812 ×5，GPIO 38）：红/绿/蓝/黄/紫/白/关
  - 语音助手对话（通过 `/api/voice_chat` 与 LLM 交互）
- 首页每 5 秒自动刷新一次，所有页面支持下拉刷新

## 设备控制说明

当前 C 固件硬件上仅蜂鸣器和 RGB 灯带可用（ESP32-S3 Octal PSRAM 占用了大量 GPIO），风扇、LED、温湿度传感器、光照传感器已禁用。

设备控制通过 Web 后端 `/api/device` 接口转发为 MQTT 命令：

| 动作 | MQTT Topic | 说明 |
|------|-----------|------|
| buzzer | `esp32/iot/cmd/buzzer` | on/off |
| face_detect | `esp32/iot/cmd/facedetect` | on/off |
| rgb | `esp32/iot/cmd/rgb` | red/green/blue/yellow/purple/white/off |

## 已知限制

- 不展示 MJPEG 实时视频流
- 不接 MQTT 和 WebSocket，只通过 HTTP 接口轮询
- 手机上真机预览或正式上线前，仍需补 HTTPS 与合法域名配置
