"""
Face detect web service.

- Receives face metadata from MQTT
- Stores recent history in SQLite
- Accepts uploaded JPEG frames from the ESP32
- Serves raw and boxed MJPEG streams
- Pushes live updates to the dashboard via WebSocket
"""

from __future__ import annotations

import base64
import io
import json
import sqlite3
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt
from flask import Flask, Response, jsonify, render_template, request
from flask_socketio import SocketIO, emit
from PIL import Image, ImageDraw


APP_VERSION = "2026.04.08-2"
DEVICE_TIMEOUT = 10
FRAME_STALE_SECONDS = 3
DETECTION_STALE_SECONDS = 1.5
MIN_VALID_UNIX_TS = 1577808000  # 2020-01-01 00:00:00
MAX_FUTURE_SKEW_SECONDS = 86400
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
FACE_TOPIC = "esp32/face_detect"
CROP_TOPIC = "esp32/face_detect/crop"
HOST = "0.0.0.0"
PORT = 8082
FRAME_INTERVAL_SECONDS = 0.05

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = BASE_DIR / "face_detect.db"

app = Flask(__name__)
app.config["SECRET_KEY"] = "face_detect_secret"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

mqtt_client = None
_device_last_seen: dict[str, float] = {}
_state_lock = threading.Lock()
_latest_frame: dict[str, Any] = {"data": None, "ts": 0.0}
_latest_detection: dict[str, Any] | None = None


def init_db() -> None:
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute(
        """
        CREATE TABLE IF NOT EXISTS detections (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device TEXT,
            timestamp REAL,
            datetime TEXT,
            frame INTEGER,
            face_count INTEGER,
            faces_json TEXT
        )
        """
    )
    c.execute(
        """
        CREATE TABLE IF NOT EXISTS faces (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            detection_id INTEGER,
            score REAL,
            x1 INTEGER, y1 INTEGER, x2 INTEGER, y2 INTEGER,
            keypoints_json TEXT,
            FOREIGN KEY (detection_id) REFERENCES detections(id)
        )
        """
    )
    c.execute(
        """
        CREATE TABLE IF NOT EXISTS face_images (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device TEXT,
            timestamp REAL,
            datetime TEXT,
            score REAL,
            x1 INTEGER, y1 INTEGER, x2 INTEGER, y2 INTEGER,
            img_jpeg BLOB
        )
        """
    )
    conn.commit()
    conn.close()


def _normalize_timestamp(ts: float | int | None) -> float:
    now = time.time()
    if ts is None:
        return now

    try:
        timestamp = float(ts)
    except (TypeError, ValueError):
        return now

    if timestamp > 1e12:
        timestamp /= 1000.0

    if timestamp < MIN_VALID_UNIX_TS or timestamp > (now + MAX_FUTURE_SKEW_SECONDS):
        return now

    return timestamp


def _ensure_datetime(ts: float | int | None) -> tuple[float, str]:
    timestamp = _normalize_timestamp(ts)
    dt = datetime.fromtimestamp(timestamp).strftime("%Y-%m-%d %H:%M:%S")
    return timestamp, dt


def save_detection(data: dict[str, Any]) -> int:
    timestamp, dt = _ensure_datetime(data.get("timestamp"))
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute(
        """
        INSERT INTO detections (device, timestamp, datetime, frame, face_count, faces_json)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (
            data["device"],
            timestamp,
            dt,
            data["frame"],
            data["face_count"],
            json.dumps(data["faces"]),
        ),
    )
    detection_id = c.lastrowid

    for face in data["faces"]:
        box = face.get("box", [0, 0, 0, 0])
        kp_json = json.dumps(face.get("keypoints", {}))
        c.execute(
            """
            INSERT INTO faces (detection_id, score, x1, y1, x2, y2, keypoints_json)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (detection_id, face.get("score", 0), box[0], box[1], box[2], box[3], kp_json),
        )

    conn.commit()
    conn.close()
    return detection_id


def get_recent_detections(limit: int = 100) -> list[dict[str, Any]]:
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute(
        """
        SELECT id, device, datetime, frame, face_count, faces_json
        FROM detections
        ORDER BY id DESC
        LIMIT ?
        """,
        (limit,),
    )
    rows = c.fetchall()
    conn.close()

    return [
        {
            "id": row[0],
            "device": row[1],
            "datetime": row[2],
            "frame": row[3],
            "face_count": row[4],
            "faces": json.loads(row[5]),
        }
        for row in rows
    ]


def get_stats() -> dict[str, Any]:
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    c.execute("SELECT COUNT(*), SUM(face_count) FROM detections")
    row = c.fetchone()
    total_detections = row[0] or 0
    total_faces = row[1] or 0

    c.execute("SELECT COUNT(DISTINCT device) FROM detections")
    devices = c.fetchone()[0] or 0

    c.execute("SELECT datetime FROM detections ORDER BY id DESC LIMIT 1")
    last = c.fetchone()
    last_time = last[0] if last else "N/A"
    conn.close()

    now = time.time()
    devices_online = sum(1 for ts in _device_last_seen.values() if now - ts <= DEVICE_TIMEOUT)

    return {
        "total_detections": total_detections,
        "total_faces": total_faces,
        "devices": devices,
        "devices_online": devices_online,
        "last_detection": last_time,
        "app_version": APP_VERSION,
    }


def save_face_image(data: dict[str, Any]) -> int | None:
    img_b64 = data.get("img_data", "")
    if not img_b64:
        return None

    rgb565 = base64.b64decode(img_b64)
    w = int(data.get("img_w", 64))
    h = int(data.get("img_h", 64))
    if len(rgb565) < w * h * 2:
        return None

    pixels = bytearray(w * h * 3)
    for i in range(w * h):
        val = (rgb565[i * 2] << 8) | rgb565[i * 2 + 1]
        r = ((val >> 11) & 0x1F) << 3
        g = ((val >> 5) & 0x3F) << 2
        b = (val & 0x1F) << 3
        pixels[i * 3] = r
        pixels[i * 3 + 1] = g
        pixels[i * 3 + 2] = b

    img = Image.frombytes("RGB", (w, h), bytes(pixels))
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=85)
    jpeg_bytes = buf.getvalue()

    timestamp, dt = _ensure_datetime(data.get("ts"))
    box = data.get("box", [0, 0, 0, 0])
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute(
        """
        INSERT INTO face_images (device, timestamp, datetime, score, x1, y1, x2, y2, img_jpeg)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            data.get("device", "unknown"),
            timestamp,
            dt,
            data.get("score", 0),
            box[0],
            box[1],
            box[2],
            box[3],
            jpeg_bytes,
        ),
    )
    face_id = c.lastrowid
    conn.commit()
    conn.close()
    return face_id


def get_recent_face_images(limit: int = 20) -> list[dict[str, Any]]:
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute(
        """
        SELECT id, device, datetime, score, x1, y1, x2, y2
        FROM face_images
        ORDER BY id DESC
        LIMIT ?
        """,
        (limit,),
    )
    rows = c.fetchall()
    conn.close()
    return [
        {
            "id": row[0],
            "device": row[1],
            "datetime": row[2],
            "score": row[3],
            "box": [row[4], row[5], row[6], row[7]],
        }
        for row in rows
    ]


def _normalize_detection(raw: dict[str, Any]) -> dict[str, Any]:
    timestamp, dt = _ensure_datetime(raw.get("ts", raw.get("timestamp")))
    return {
        "device": raw.get("device", "unknown"),
        "timestamp": timestamp,
        "datetime": dt,
        "frame": int(raw.get("frame", 0)),
        "face_count": int(raw.get("count", raw.get("face_count", 0))),
        "faces": raw.get("faces", []),
        "img_w": int(raw.get("img_w", 320)),
        "img_h": int(raw.get("img_h", 240)),
    }


def _set_latest_detection(data: dict[str, Any]) -> None:
    global _latest_detection
    latest = dict(data)
    latest["received_at"] = time.time()
    with _state_lock:
        _latest_detection = latest


def _get_latest_detection() -> dict[str, Any] | None:
    with _state_lock:
        if not _latest_detection:
            return None
        latest = dict(_latest_detection)
    if time.time() - latest["received_at"] > DETECTION_STALE_SECONDS:
        return None
    latest.pop("received_at", None)
    return latest


def _get_latest_frame() -> tuple[bytes | None, float]:
    with _state_lock:
        return _latest_frame["data"], _latest_frame["ts"]


def _score_color(score: float) -> tuple[int, int, int]:
    if score > 0.8:
        return (34, 197, 94)
    if score > 0.6:
        return (234, 179, 8)
    return (239, 68, 68)


def _draw_boxes_on_jpeg(frame_data: bytes, detection: dict[str, Any] | None) -> bytes:
    if not frame_data:
        return b""

    image = Image.open(io.BytesIO(frame_data)).convert("RGB")
    if detection and detection.get("faces"):
        draw = ImageDraw.Draw(image)
        scale_x = image.width / max(detection.get("img_w", image.width), 1)
        scale_y = image.height / max(detection.get("img_h", image.height), 1)
        for face in detection["faces"]:
            box = face.get("box", [0, 0, 0, 0])
            x1 = int(box[0] * scale_x)
            y1 = int(box[1] * scale_y)
            x2 = int(box[2] * scale_x)
            y2 = int(box[3] * scale_y)
            color = _score_color(float(face.get("score", 0)))
            draw.rectangle((x1, y1, x2, y2), outline=color, width=3)
            draw.text((x1 + 4, max(0, y1 - 14)), f"{float(face.get('score', 0)):.2f}", fill=color)

    buf = io.BytesIO()
    image.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def _mjpeg_generator(boxed: bool):
    while True:
        frame_data, frame_ts = _get_latest_frame()
        if frame_data and (time.time() - frame_ts) <= FRAME_STALE_SECONDS:
            detection = _get_latest_detection() if boxed else None
            output = _draw_boxes_on_jpeg(frame_data, detection) if boxed else frame_data
            yield (
                b"--frame\r\n"
                b"Content-Type: image/jpeg\r\n"
                b"Content-Length: "
                + str(len(output)).encode()
                + b"\r\n\r\n"
                + output
                + b"\r\n"
            )
        time.sleep(FRAME_INTERVAL_SECONDS)


def on_mqtt_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[mqtt] connected to {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(FACE_TOPIC)
        client.subscribe(CROP_TOPIC)
    else:
        print(f"[mqtt] connect failed: {rc}")


def on_mqtt_message(client, userdata, msg):
    try:
        raw = json.loads(msg.payload.decode())
        device_id = raw.get("device", "unknown")
        _device_last_seen[device_id] = time.time()

        if msg.topic == CROP_TOPIC:
            face_id = save_face_image(raw)
            if face_id:
                timestamp, dt = _ensure_datetime(raw.get("ts"))
                socketio.emit(
                    "new_face_image",
                    {
                        "id": face_id,
                        "device": device_id,
                        "datetime": dt,
                        "timestamp": timestamp,
                        "score": raw.get("score", 0),
                        "box": raw.get("box", []),
                        "img_url": f"/api/face_image/{face_id}",
                    },
                    namespace="/",
                )
        else:
            data = _normalize_detection(raw)
            save_detection(data)
            _set_latest_detection(data)
            socketio.emit("new_detection", data, namespace="/")
            socketio.emit("overlay_update", data, namespace="/")
    except Exception as exc:
        print(f"[mqtt] message handling failed: {exc}")


def start_mqtt() -> None:
    global mqtt_client
    mqtt_client = mqtt.Client(client_id="web_face_server")
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_message = on_mqtt_message
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_forever()
    except Exception as exc:
        print(f"[mqtt] fatal error: {exc}")


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/upload", methods=["POST"])
def upload():
    if request.content_type != "image/jpeg":
        return "bad content-type", 400
    data = request.get_data()
    if not data:
        return "no data", 400

    with _state_lock:
        _latest_frame["data"] = data
        _latest_frame["ts"] = time.time()
    return "ok"


@app.route("/stream")
def stream():
    return Response(_mjpeg_generator(boxed=False), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/stream_boxed")
def stream_boxed():
    return Response(_mjpeg_generator(boxed=True), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/api/detections")
def api_detections():
    limit = request.args.get("limit", 100, type=int)
    return jsonify(get_recent_detections(limit))


@app.route("/api/stats")
def api_stats():
    return jsonify(get_stats())


@app.route("/api/latest_detection")
def api_latest_detection():
    return jsonify(_get_latest_detection() or {})


@app.route("/api/face_image/<int:face_id>")
def api_face_image(face_id):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT img_jpeg FROM face_images WHERE id = ?", (face_id,))
    row = c.fetchone()
    conn.close()
    if row and row[0]:
        return Response(row[0], mimetype="image/jpeg")
    return "Not found", 404


@app.route("/api/face_images")
def api_face_images():
    limit = request.args.get("limit", 20, type=int)
    return jsonify(get_recent_face_images(limit))


@socketio.on("connect")
def handle_connect():
    emit("connected", {"status": "ok"})
    latest = _get_latest_detection()
    if latest:
        emit("overlay_update", latest)


if __name__ == "__main__":
    init_db()
    mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
    mqtt_thread.start()
    print(f"[web] dashboard ready at http://{HOST}:{PORT}")
    socketio.run(app, host=HOST, port=PORT, debug=False, allow_unsafe_werkzeug=True)
