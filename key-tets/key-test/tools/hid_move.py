#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Send commands to the key-test HID device.

Feature report 0xA0, 16 bytes:
  move : [0xA0][0x13][int32 target, 0.001 mm]...
  home : [0xA0][0x14]...
  jog  : [0xA0][0x15][int32 delta, 0.001 mm]...

Usage:
    python hid_move.py 10       # absolute target +10 mm (after homing)
    python hid_move.py 250.5    # absolute target +250.5 mm
    python hid_move.py reset    # home: move negative until the switch, max 300 mm
    python hid_move.py jog 2    # manual relative jog before homing, max 5 mm
"""

import ctypes
import struct
import sys
from ctypes import wintypes

import hid_key_detect as hk

REPORT_ID = 0xA0
CMD_MOVE = 0x13
CMD_RESET = 0x14
CMD_JOG = 0x15


def send_cmd(handle, cmd, um=0):
    buf = (ctypes.c_ubyte * 16)()
    buf[0] = REPORT_ID
    buf[1] = cmd
    if cmd in (CMD_MOVE, CMD_JOG):
        struct.pack_into("<i", buf, 2, um)

    hk.hid.HidD_SetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    hk.hid.HidD_SetFeature.restype = wintypes.BOOL
    return bool(hk.hid.HidD_SetFeature(
        handle, ctypes.cast(buf, ctypes.c_void_p), len(buf)
    ))


def main():
    if len(sys.argv) not in (2, 3):
        print("usage: python hid_move.py <mm>|reset|jog <mm>")
        return 1

    arg = sys.argv[1].strip().lower()
    cmd = CMD_MOVE
    um = 0
    mm = 0.0

    if arg == "reset":
        cmd = CMD_RESET
    elif arg == "jog":
        if len(sys.argv) != 3:
            print("usage: python hid_move.py jog <mm>")
            return 1
        cmd = CMD_JOG
        mm = float(sys.argv[2])
        um = int(round(mm * 1000.0))
    else:
        mm = float(arg)
        um = int(round(mm * 1000.0))

    um = max(-2000000000, min(2000000000, um))

    found = hk.find_device()
    if found is None:
        print("device 0483:572B not found")
        return 1

    handle, _path = found
    try:
        ok = send_cmd(handle, cmd, um)
        if cmd == CMD_RESET:
            print(f"home -> {'ok' if ok else 'failed'}")
        elif cmd == CMD_JOG:
            print(f"jog {mm:+.3f} mm ({um} um) -> {'ok' if ok else 'failed'}")
        else:
            print(f"target {mm:+.3f} mm ({um} um) -> {'ok' if ok else 'failed'}")
        return 0 if ok else 1
    finally:
        hk.kernel32.CloseHandle(handle)


if __name__ == "__main__":
    raise SystemExit(main())
