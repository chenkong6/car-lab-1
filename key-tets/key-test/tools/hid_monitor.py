#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Live position/status monitor for the key-test HID device.

Input report 2: [2][int32 position, 0.001 mm][status][0]
status: bit0 moving, bit1 key pressed, bit2 at zero.

Run: python hid_monitor.py
"""

import ctypes
import threading
import time
from ctypes import wintypes

import hid_key_detect as hk


def print_report(data):
    if len(data) >= 7 and data[0] == 2:
        pos = int.from_bytes(data[1:5], "little", signed=True)
        status = data[5]
        error = data[6]
        print(
            f"\rpos={pos / 1000:+9.3f} mm  moving={status & 1}  "
            f"key={(status >> 1) & 1}  zero={(status >> 2) & 1}  "
            f"homed={(status >> 3) & 1}  err={error}",
            end="",
            flush=True,
        )
    elif len(data) >= 7 and data[0] == 1:
        pressed = (data[6] & 0x01) == 0
        print(f"\n[{'PRESS' if pressed else 'RELEASE'}]", flush=True)


def reader(handle):
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

        print_report(bytes(buf[:read.value]))


def main():
    found = hk.find_device()
    if found is None:
        print("device 0483:572B not found")
        return 1

    handle, path = found
    print(f"connected: {path}")

    thread = threading.Thread(target=reader, args=(handle,), daemon=True)
    thread.start()

    try:
        while thread.is_alive():
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        hk.kernel32.CloseHandle(handle)
        print("\nbye")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
