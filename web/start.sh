#!/bin/bash
set -e

# Start the unified face-detect web service.

cd "$(dirname "$0")"

echo "[web] installing dependencies..."
python3 -m pip install -r requirements.txt -q

echo "[web] starting dashboard on :8082 ..."
python3 app.py
