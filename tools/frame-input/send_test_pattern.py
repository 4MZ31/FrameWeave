# SPDX-License-Identifier: MIT
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors.
"""Send a moving test pattern into PushFPS through the Frame Input API (docs/FRAME-INPUT-API.md).

Windows only, Python 3.8+, no extra packages. Run it, then in PushFPS choose Other app on the home
screen (or in streaming / video playback mode) and pick "PushFPS test pattern".

    python send_test_pattern.py [--channel default] [--width 1280] [--height 720] [--fps 30]
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import mmap
import struct
import sys
import time

MAGIC, VERSION, SLOTS, HEADER = 0x4E494650, 1, 3, 4096
SLOT_OFFSET, SLOT_SIZE = 3904, 64          # pushfps_frame_input_header.slots, sizeof(slot)
LATEST, COUNTER, NAME = 48, 56, 64          # latestSlot, frameCounter, senderName offsets
BGRA8 = 1

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateEventW.restype = wt.HANDLE
kernel32.SetEvent.argtypes = [wt.HANDLE]
kernel32.QueryPerformanceCounter.argtypes = [ctypes.POINTER(ctypes.c_int64)]
kernel32.QueryPerformanceFrequency.argtypes = [ctypes.POINTER(ctypes.c_int64)]


def qpc100ns():
    now, freq = ctypes.c_int64(), ctypes.c_int64()
    kernel32.QueryPerformanceCounter(ctypes.byref(now))
    kernel32.QueryPerformanceFrequency(ctypes.byref(freq))
    return now.value * 10_000_000 // freq.value


def main():
    if sys.platform != "win32":
        sys.exit("The Frame Input API uses Windows shared memory.")
    ap = argparse.ArgumentParser()
    ap.add_argument("--channel", default="default")
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--fps", type=int, default=30)
    a = ap.parse_args()
    w, h = a.width, a.height
    slot_bytes = w * h * 4
    name = "Local\\PushFPS.FrameInput." + a.channel
    # mmap with a tag name creates a named mapping backed by the page file.
    view = mmap.mmap(-1, HEADER + SLOTS * slot_bytes, tagname=name)
    ready = kernel32.CreateEventW(None, False, False, name + ".Ready")
    struct.pack_into("<IIII II Q II II", view, 0, 0, VERSION, HEADER, SLOTS, w, h, slot_bytes, a.fps, 1,
                     ctypes.windll.kernel32.GetCurrentProcessId(), 0)
    label = b"PushFPS test pattern"
    view[NAME:NAME + len(label) + 1] = label + b"\0"
    struct.pack_into("<I", view, 0, MAGIC)      # last: the channel is valid now
    row = bytearray(w * 4)
    frame, latest = 0, 0
    print(f"Sending {w}x{h} at {a.fps} FPS on channel '{a.channel}'. Ctrl+C stops.")
    try:
        while True:
            start = time.perf_counter()
            frame += 1
            index = (latest + 1) % SLOTS
            slot = SLOT_OFFSET + index * SLOT_SIZE
            seq = struct.unpack_from("<I", view, slot)[0]
            struct.pack_into("<I", view, slot, seq + 1)            # odd: writing
            base = HEADER + index * slot_bytes
            bar = (frame * 8) % w                                   # a white bar moving right
            for y in range(h):
                shade = (y * 255 // h) & 0xFF
                row[:] = bytes((shade, 40, 255 - shade, 255)) * w
                row[bar * 4:min(w, bar + 24) * 4] = b"\xff\xff\xff\xff" * (min(w, bar + 24) - bar)
                view[base + y * w * 4: base + (y + 1) * w * 4] = row
            struct.pack_into("<IIIIIqQ", view, slot + 4, w, h, w * 4, BGRA8, 0, qpc100ns(), frame)
            struct.pack_into("<I", view, slot, seq + 2)            # even: complete
            struct.pack_into("<I", view, LATEST, index)
            counter = struct.unpack_from("<Q", view, COUNTER)[0]
            struct.pack_into("<Q", view, COUNTER, counter + 1)
            kernel32.SetEvent(ready)
            latest = index
            time.sleep(max(0.0, 1.0 / a.fps - (time.perf_counter() - start)))
    except KeyboardInterrupt:
        pass
    finally:
        struct.pack_into("<I", view, 0, 0)
        view.close()


if __name__ == "__main__":
    main()
