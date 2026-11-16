#!/usr/bin/env python3
"""Capture the BF-F8HP Pro 160x128 LCD over the programming UART.

The firmware exposes a 4bpp shadow framebuffer with a simple RLE stream:
  A5 5A <w:be16> <h:be16> <bpp> then (count,value)* and 00 00.

Examples:
  python3 tools/screencap.py --port /dev/ttyUSB0 --out screen.bmp
  python3 tools/screencap.py --port /dev/ttyUSB0 --stream --fps 2 --out frames/
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: python3 -m pip install pyserial")


PALETTE_565 = {
    0: 0x0000,  # black
    1: 0xFFFF,  # white
    2: 0x33DF,  # blue rail
    3: 0x8CB4,  # dim gray
    4: 0xF800,  # red
    5: 0x3A09,  # dark gray soft bar
    6: 0x52AC,  # idle box gray
}


def rgb888(c565: int) -> tuple[int, int, int]:
    r = (c565 >> 11) & 0x1F
    g = (c565 >> 5) & 0x3F
    b = c565 & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


PALETTE = {idx: rgb888(c) for idx, c in PALETTE_565.items()}


def read_magic(ser: serial.Serial, timeout: float = 2.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        b = ser.read(1)
        if b != b"\xA5":
            continue
        b2 = ser.read(1)
        if b2 == b"\x5A":
            return
    raise TimeoutError("no screen-capture header (firmware built with send_screen?)")


def read_frame(ser: serial.Serial) -> tuple[int, int, bytearray]:
    read_magic(ser)
    dims = ser.read(4)
    if len(dims) != 4:
        raise TimeoutError("short width/height")
    w, h = struct.unpack(">HH", dims)
    bpp = ser.read(1)
    if bpp != b"\x04":
        raise ValueError(f"unsupported bpp {bpp!r}")
    buf = bytearray((w * h) // 2)

    pos = 0
    while pos < len(buf):
        pair = ser.read(2)
        if len(pair) != 2:
            raise TimeoutError("short RLE pair")
        count, value = pair
        if count == 0:
            break
        end = min(pos + count, len(buf))
        buf[pos:end] = bytes([value]) * (end - pos)
        pos = end
    if pos != len(buf):
        raise ValueError(f"short frame: {pos}/{len(buf)} bytes")
    return w, h, buf


def write_bmp(path: Path, w: int, h: int, buf: bytearray) -> None:
    row_raw = w * 3
    row_size = (row_raw + 3) & ~3
    image_size = row_size * h
    file_size = 54 + image_size

    header = bytearray()
    header += b"BM"
    header += struct.pack("<IHHI", file_size, 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, image_size, 2835, 2835, 0, 0)

    rows = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            p = y * w + x
            byte = buf[p >> 1]
            idx = (byte >> 4) if (p & 1) else (byte & 0x0F)
            r, g, b = PALETTE.get(idx, PALETTE[1])
            row += bytes((b, g, r))
        row += b"\x00" * (row_size - len(row))
        rows += row

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + rows)


def capture(ser: serial.Serial) -> tuple[int, int, bytearray]:
    ser.reset_input_buffer()
    ser.write(b"s")
    ser.flush()
    return read_frame(ser)


def main() -> None:
    ap = argparse.ArgumentParser(description="BF-F8HP Pro LCD screen capture")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", type=Path, default=Path("screen.bmp"))
    ap.add_argument("--stream", action="store_true", help="repeat captures")
    ap.add_argument("--fps", type=float, default=2.0)
    ap.add_argument("--frames", type=int, default=0, help="0 = until Ctrl-C")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=2.0) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        if not args.stream:
            w, h, buf = capture(ser)
            write_bmp(args.out, w, h, buf)
            print(f"{args.out} ({w}x{h})")
            return

        args.out.mkdir(parents=True, exist_ok=True)
        n = 0
        try:
            while args.frames == 0 or n < args.frames:
                w, h, buf = capture(ser)
                path = args.out / f"frame_{n:04d}.bmp"
                write_bmp(path, w, h, buf)
                print(path)
                n += 1
                time.sleep(max(0.0, 1.0 / args.fps))
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
