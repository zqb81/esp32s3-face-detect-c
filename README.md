# ESP32-S3 Face Detect (C / ESP-IDF)

ESP32-S3 real-time face detection project based on ESP-IDF and `esp-dl`.

The current implementation uses the official `esp-dl` two-stage human face detector:

- `face_msr.espdl`
- `face_mnp.espdl`

These models are packed into a single raw partition image during build and flashed to the `model` partition.

## Hardware

| Part | Model |
|------|------|
| MCU | ESP32-S3-WROOM-1 N16R8 |
| Camera | OV5640 |
| Display | ST7735 1.8" TFT (128x160) |

## Architecture

```text
Camera (QVGA JPEG)
  -> buffer pool copy on Core 0
  -> JPEG decode on Core 1
  -> esp-dl face detect (MSR + MNP)
  -> TFT display overlay
  -> MQTT result / crop upload
  -> HTTP upload to remote service
```

## Key Files

| File | Purpose |
|------|------|
| `main/main.c` | App entry, Wi-Fi, task scheduling, buffer pool |
| `main/camera.c` | OV5640 driver |
| `main/display.c` | ST7735 display driver |
| `main/face_detect.cpp` | `esp-dl` face detection wrapper |
| `main/http_stream.c` | Local HTTP server and remote JPEG upload |
| `main/mqtt_comm.c` | MQTT face result and crop upload |
| `main/config.h` | Pins and runtime configuration |

## Models

The project now uses the official ESP32-S3-compatible human face detection models:

- `main/models/face_msr.espdl`
- `main/models/face_mnp.espdl`

At build time, the top-level `CMakeLists.txt` runs:

- `managed_components/espressif__esp-dl/fbs_loader/pack_espdl_models.py`

This generates a packed blob such as:

- `build-codex/face_models.espdl`

That blob is flashed directly to the `model` partition defined in `partitions.csv` at `0x310000`.

Important: this is not a SPIFFS-mounted model filesystem. `esp-dl` loads the models from the raw flash partition.

## Build

Typical ESP-IDF flow:

```bash
idf.py set-target esp32s3
idf.py build
```

For this repository, flashing the firmware also flashes the packed model partition automatically.

## Flash

```bash
idf.py -p <PORT> flash monitor
```

On the Windows environment used for validation, the working command pattern was:

```powershell
python <IDF_PATH>\tools\idf.py -B build-codex -p COM8 flash
```

## Verified State

The latest verified flash completed successfully with:

- app flashed to `0x10000`
- packed face models flashed to `0x310000`

Serial logs confirmed that the old `Unsupported format` model error is gone. The current successful model-load signal is that `dl::Model: Minimize()` warnings appear twice during startup, once per model.

## Runtime Notes

- Face model loading is fixed and working.
- The remote upload target `http://101.33.209.65:8081/upload` still intermittently fails with connection reset / connect errors.
- Upload failure logs are rate-limited to reduce log spam.

## Features

- [x] OV5640 camera capture
- [x] ST7735 display output
- [x] Wi-Fi STA mode
- [x] MQTT face result upload
- [x] MQTT face crop upload
- [x] Official `esp-dl` MSR + MNP face detection
- [x] Automatic model partition packing and flashing
- [ ] Stable remote HTTP upload service
- [ ] Additional runtime diagnostics

## Dependencies

- ESP-IDF 5.4.x
- `espressif/esp32-camera`
- `espressif/esp-dl`

Dependencies are resolved through `idf_component.yml`.
