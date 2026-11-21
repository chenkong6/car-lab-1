#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Home, go to 300 mm, go back to 0, repeat.

Requires hid_demo_server.py to be running (it reads the telemetry and serves
http://127.0.0.1:8000).

Run:  python hid_cycle.py
"""

import json
import time
import urllib.request

import hid_key_detect as hk
import hid_move as hm

API = "http://127.0.0.1:8000/api/state"
TARGET_MAX_UM = 300000
TOLERANCE_UM = 800


def get_state():
    with urllib.request.urlopen(API, timeout=1.0) as response:
        return json.load(response)


def wait_for(predicate, timeout, description):
    start = time.time()
    while time.time() - start < timeout:
        try:
            state = get_state()
        except Exception:
            time.sleep(0.2)
            continue
        if predicate(state):
            return state
        time.sleep(0.05)
    raise TimeoutError(description)


def send(handle, command, um=0):
    if not hm.send_cmd(handle, command, um):
        raise RuntimeError("HID feature report failed")


def home(handle):
    send(handle, hm.CMD_RESET)
    time.sleep(0.3)
    state = wait_for(
        lambda s: s.get("connected") and ((s.get("homed") and not s.get("moving")) or s.get("error")),
        45.0,
        "home timeout",
    )
    if state.get("error"):
        raise RuntimeError(f"home failed, err={state['error']}")


def move_to(handle, target_um):
    send(handle, hm.CMD_MOVE, target_um)
    time.sleep(0.2)
    state = wait_for(
        lambda s: s.get("connected")
        and (
            (not s.get("moving") and abs(s.get("pos_mm", 0) * 1000 - target_um) < TOLERANCE_UM)
            or s.get("error")
        ),
        30.0,
        f"move to {target_um / 1000:.1f} mm timeout",
    )
    if state.get("error"):
        raise RuntimeError(f"move failed, err={state['error']}")


def main():
    try:
        get_state()
    except Exception:
        print("start hid_demo_server.py first (http://127.0.0.1:8000)")
        return 1

    found = hk.find_device()
    if found is None:
        print("device 0483:572B not found")
        return 1

    handle, _path = found
    print("cycle: home -> 300 mm -> 0 mm -> repeat")

    try:
        while True:
            home(handle)
            print("home done")

            move_to(handle, TARGET_MAX_UM)
            print("300 mm done")

            move_to(handle, 0)
            print("0 mm done")
    except KeyboardInterrupt:
        print("\ncycle stopped")
    except Exception as exc:
        print(f"\ncycle aborted: {exc}")
        return 1
    finally:
        hk.kernel32.CloseHandle(handle)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
