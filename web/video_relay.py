"""
Deprecated compatibility entrypoint.

The relay-only service has been merged into web/app.py. Keep this file so
older deployment notes fail clearly instead of silently serving the wrong app.
"""

from __future__ import annotations


def main() -> int:
    print("video_relay.py is deprecated.")
    print("Use `python3 app.py` instead.")
    print("The unified web service now provides:")
    print("- POST /upload")
    print("- GET  /stream")
    print("- GET  /stream_boxed")
    print("- GET  /")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
