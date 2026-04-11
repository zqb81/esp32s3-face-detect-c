# Changelog

## Unreleased
- 优化设备端性能链路：HTTP 上传与人脸裁剪图 MQTT 发布改为异步处理
- 新增 `crop` 阶段耗时统计，便于观察抓拍提交流程开销
- 修复人脸裁剪图 Base64 长度探测回归，恢复 `esp32/face_detect/crop` 持续发布
- 更新项目文档，补充抓拍图静默时的排查方法

## v0.5.0 - SCRFD model via SPIFFS partition
- Add SPIFFS partition image for main/models (CMake)
- Prepare SCRFD .espdl placement under main/models/

