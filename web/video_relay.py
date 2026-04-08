"""
简易视频中转服务：接收 ESP32 POST 的 JPEG 帧，并提供 MJPEG 流
访问：
- POST /upload (Content-Type: image/jpeg)
- GET  /stream  (MJPEG)
- GET  /         (简单页面)
"""
from flask import Flask, request, Response
from threading import Lock
import time

app = Flask(__name__)
latest = {"data": None, "ts": 0}
lock = Lock()

INDEX_HTML = """<!doctype html><html><head><meta charset='utf-8'>
<title>ESP32 Relay</title></head><body style='margin:0;background:#111;color:#eee'>
<h3 style='padding:10px'>ESP32 MJPEG Relay</h3>
<img src='/stream' />
</body></html>"""

@app.route('/')
def index():
    return INDEX_HTML

@app.route('/upload', methods=['POST'])
def upload():
    if request.content_type != 'image/jpeg':
        return 'bad content-type', 400
    data = request.data
    if not data:
        return 'no data', 400
    with lock:
        latest["data"] = data
        latest["ts"] = time.time()
    return 'ok'

@app.route('/stream')
def stream():
    def gen():
        while True:
            with lock:
                frame = latest["data"]
            if frame:
                yield (b'--frame\r\n'
                       b'Content-Type: image/jpeg\r\n'
                       b'Content-Length: ' + str(len(frame)).encode() + b'\r\n\r\n' +
                       frame + b'\r\n')
            time.sleep(0.05)
    return Response(gen(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8081, threaded=True)
