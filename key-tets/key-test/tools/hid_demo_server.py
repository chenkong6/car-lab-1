#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Serve the slide-stage status page and stream HID telemetry to it.

Run:  python hid_demo_server.py [port]
Then open http://127.0.0.1:8000
"""

import ctypes
import json
import sys
import threading
import time
from ctypes import wintypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import hid_key_detect as hk

HERE = Path(__file__).resolve().parent
INDEX = HERE / "demo" / "index.html"

lock = threading.Lock()
state = {
    "connected": False,
    "pos_mm": 0.0,
    "moving": False,
    "key": False,
    "zero": False,
    "homed": False,
    "error": 0,
    "path": "",
    "ts": 0.0,
}


def set_state(**kwargs):
    with lock:
        state.update(kwargs)
        state["ts"] = time.time()


def read_loop(handle):
    buf = (ctypes.c_ubyte * 64)()
    read = wintypes.DWORD(0)

    while True:
        if not hk.kernel32.ReadFile(
            handle,
            ctypes.cast(buf, ctypes.c_void_p),
            ctypes.sizeof(buf),
            ctypes.byref(read),
            None,
        ):
            return

        data = bytes(buf[:read.value])
        if len(data) >= 7 and data[0] == 2:
            status = data[5]
            set_state(
                connected=True,
                pos_mm=int.from_bytes(data[1:5], "little", signed=True) / 1000.0,
                moving=bool(status & 0x01),
                key=bool(status & 0x02),
                zero=bool(status & 0x04),
                homed=bool(status & 0x08),
                error=data[6],
            )


def device_loop():
    while True:
        found = hk.find_device()
        if found is None:
            set_state(connected=False, moving=False)
            time.sleep(1.0)
            continue

        handle, path = found
        set_state(connected=True, path=path, error=0, moving=False)
        try:
            read_loop(handle)
        except Exception:
            pass
        finally:
            hk.kernel32.CloseHandle(handle)
            set_state(connected=False, moving=False)
        time.sleep(0.5)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = self.path.split("?")[0]

        if path == "/api/state":
            with lock:
                payload = json.dumps(state).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return

        if path in ("/", "/index.html"):
            body = INDEX.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(404)
        self.end_headers()

    def log_message(self, *args):
        pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000

    threading.Thread(target=device_loop, daemon=True).start()

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"serving http://127.0.0.1:{port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
