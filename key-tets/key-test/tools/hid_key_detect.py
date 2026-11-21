#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Detect button reports from the custom HID device (VID 0x0483 / PID 0x572B).

Report layout: report ID 1 + throttle + X(2) + Y(2) + buttons.
Button bit 0 = the PB12 key.

Run:  python hid_key_detect.py
"""

import ctypes
import threading
import time
from ctypes import wintypes

VID = 0x0483
PID = 0x572B

DIGCF_PRESENT = 0x02
DIGCF_DEVICEINTERFACE = 0x10
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x01
FILE_SHARE_WRITE = 0x02
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_NO_MORE_ITEMS = 259

setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
hid = ctypes.WinDLL("hid", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("InterfaceClassGuid", GUID),
        ("Flags", wintypes.DWORD),
        ("Reserved", ctypes.c_void_p),
    ]


class SP_DEVICE_INTERFACE_DETAIL_DATA_W(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("DevicePath", ctypes.c_wchar * 1),
    ]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [
        ("Size", wintypes.ULONG),
        ("VendorID", wintypes.USHORT),
        ("ProductID", wintypes.USHORT),
        ("VersionNumber", wintypes.USHORT),
    ]


setupapi.SetupDiGetClassDevsW.restype = wintypes.HANDLE
setupapi.SetupDiGetClassDevsW.argtypes = [
    ctypes.POINTER(GUID), wintypes.LPCWSTR, wintypes.HWND, wintypes.DWORD,
]
setupapi.SetupDiEnumDeviceInterfaces.restype = wintypes.BOOL
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(GUID),
    wintypes.DWORD, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA),
]
setupapi.SetupDiGetDeviceInterfaceDetailW.restype = wintypes.BOOL
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [
    wintypes.HANDLE, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA),
    ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
    ctypes.c_void_p,
]
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]

hid.HidD_GetHidGuid.argtypes = [ctypes.POINTER(GUID)]
hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]

kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
    wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
]
kernel32.ReadFile.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]


def find_device():
    """Return (handle, device_path) for the first matching HID, else None."""
    hid_guid = GUID()
    hid.HidD_GetHidGuid(ctypes.byref(hid_guid))

    hdev = setupapi.SetupDiGetClassDevsW(
        ctypes.byref(hid_guid), None, None,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE,
    )
    if hdev == INVALID_HANDLE_VALUE:
        return None

    try:
        index = 0
        while True:
            iface = SP_DEVICE_INTERFACE_DATA()
            iface.cbSize = ctypes.sizeof(iface)
            if not setupapi.SetupDiEnumDeviceInterfaces(
                hdev, None, ctypes.byref(hid_guid), index, ctypes.byref(iface)
            ):
                return None
            index += 1

            detail = ctypes.create_string_buffer(2048)
            ctypes.memmove(detail, ctypes.byref(wintypes.DWORD(
                ctypes.sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)
            )), 4)

            if not setupapi.SetupDiGetDeviceInterfaceDetailW(
                hdev, ctypes.byref(iface), ctypes.cast(detail, ctypes.c_void_p),
                len(detail), None, None,
            ):
                continue

            path = ctypes.wstring_at(ctypes.addressof(detail) + 4)
            handle = kernel32.CreateFileW(
                path,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                None,
                OPEN_EXISTING,
                0,
                None,
            )
            if handle == INVALID_HANDLE_VALUE:
                continue

            attrs = HIDD_ATTRIBUTES()
            attrs.Size = ctypes.sizeof(attrs)
            if (hid.HidD_GetAttributes(handle, ctypes.byref(attrs))
                    and attrs.VendorID == VID and attrs.ProductID == PID):
                return handle, path

            kernel32.CloseHandle(handle)
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(hdev)


def read_loop(handle, stop):
    buf = (ctypes.c_ubyte * 64)()
    read = wintypes.DWORD(0)
    buttons = 0
    first = True

    while not stop.is_set():
        if not kernel32.ReadFile(
            handle, ctypes.cast(buf, ctypes.c_void_p),
            ctypes.sizeof(buf), ctypes.byref(read), None,
        ):
            return

        if read.value >= 1 and first:
            first = False
            data = bytes(buf[:read.value])
            print(f"first report: {data.hex(' ')}", flush=True)

        if read.value >= 7 and buf[0] == 1:
            pressed = 1 if (buf[6] & 0x01) == 0 else 0
            if pressed != buttons:
                buttons = pressed
                stamp = time.strftime("%H:%M:%S")
                print(f"[{stamp}] {'PRESS' if pressed else 'RELEASE'} (button byte={buf[6]})", flush=True)


def main():
    print(f"looking for HID {VID:04X}:{PID:04X} ...", flush=True)
    stop = threading.Event()

    try:
        while True:
            found = find_device()
            if found is None:
                time.sleep(1.0)
                continue

            handle, path = found
            print(f"connected: {path}", flush=True)

            reader = threading.Thread(target=read_loop, args=(handle, stop), daemon=True)
            reader.start()

            while reader.is_alive():
                time.sleep(0.2)

            kernel32.CloseHandle(handle)
            print("disconnected, retrying ...", flush=True)
            time.sleep(0.5)
    except KeyboardInterrupt:
        stop.set()
        print("\nbye", flush=True)


if __name__ == "__main__":
    main()
