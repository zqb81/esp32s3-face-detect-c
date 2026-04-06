# 人脸检测模型获取指南

## 📋 模型需求

ESP32 人脸检测项目需要 SCRFD (Sample and Computation Redistribution for Face Detection) 模型。

## 🔧 获取方式

### 方式1：从 esp-dl 示例获取（推荐）
1. 访问 [esp-dl GitHub 仓库](https://github.com/espressif/esp-dl)
2. 进入 `examples/human_face_detect/` 目录
3. 查找预编译的 `.espdl` 模型文件
4. 下载并重命名为 `scrfd_face_detect.espdl`

### 方式2：使用 SCRFD 官方模型转换
1. 下载原始 SCRFD 模型（ONNX 格式）：
   - [SCRFD GitHub](https://github.com/deepinsight/insightface/tree/master/detection/scrfd)
   - 推荐：`scrfd_500m_bnkps.onnx`（轻量级，适合 ESP32）
2. 使用 esp-dl 工具转换为 `.espdl` 格式：
   ```bash
   # 需要安装 esp-dl 转换工具
   python3 -m pip install esp-dl
   python3 -m esp_dl.convert --model scrfd_500m_bnkps.onnx --output scrfd_face_detect.espdl
   ```

### 方式3：使用预转换模型
1. 搜索 GitHub 上的 `scrfd_esp32.espdl` 或类似文件
2. 确保模型支持 ESP32-S3 和 OV5640 摄像头

## 📁 目录结构

将下载的模型文件放到：
```
main/models/
├── scrfd_face_detect.espdl    # 主模型文件
└── README.md                  # 本文件
```

## ⚙️ 模型配置

在 `face_detect.cpp` 中取消注释相应代码：

```cpp
// 方式1: 从 RODATA (模型编译进固件)
// dl::Model *model = new dl::Model((const char *)model_data_rodata);
// model->build(0, dl::MEMORY_MANAGER_GREEDY);

// 方式2: 从 Flash 分区（推荐）
dl::Model *model = new dl::Model("scrfd_face_detect", dl::fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
model->build(0, dl::MEMORY_MANAGER_GREEDY);
```

## 🔍 验证模型

1. 编译项目：`idf.py build`
2. 烧录到 ESP32-S3
3. 检查日志输出：
   ```
   I (xxx) FACE_DETECT: 加载 SCRFD 模型成功
   I (xxx) FACE_DETECT: 模型输入尺寸: 320x240
   ```

## 📞 支持

- ESP-DL 文档：https://docs.espressif.com/projects/esp-dl
- SCRFD 论文：https://arxiv.org/abs/2103.14030
- 项目 Issues：在 GitHub 仓库提交问题

---
*最后更新：2026-04-05*
*由 Ping 自动生成*