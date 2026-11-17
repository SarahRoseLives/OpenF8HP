#!/usr/bin/env python3
"""Flash the app through the resident RAM stub, no update mode.

Usage:
  python3 tools/flash_live.py --port /dev/ttyUSB0 firmware/hello/hello.kdhx
  python3 tools/flash_live.py --port /dev/ttyUSB0 firmware/hello/hello.bin

The app must contain the RAM stub.  KDH update mode remains the recovery path.
"""
from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: python3 -m pip install pyserial")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kdhx  # noqa: E402


def read_until(ser: serial.Serial, needle: bytes, timeout: float = 8.0) -> bytes:
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue
        buf += b
        if buf.endswith(needle):
            return bytes(buf)
    raise TimeoutError(f"timed out waiting for {needle!r}; got {bytes(buf)!r}")


def main() -> None:
    ap = argparse.ArgumentParser(description="OpenF8HP RAM-stub flasher")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tnc", action="store_true",
                    help="TNC image: enter stub with KISS FEND FF FEND")
    ap.add_argument("image", type=Path)
    args = ap.parse_args()

    raw = args.image.read_bytes()
    data = kdhx.decode(raw) if args.image.suffix.lower() == ".kdhx" else raw
    if len(data) < 1024 or len(data) > 0x1F000 or (len(data) & 1):
        sys.exit(f"unsupported image length {len(data)}")
    crc = zlib.crc32(data) & 0xFFFFFFFF

    with serial.Serial(args.port, args.baud, timeout=2.0) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(b"\xC0\xFF\xC0" if args.tnc else b"x")
        ser.flush()
        read_until(ser, b"RFSTUB\n")
        ser.write(b"FLSH" + struct.pack("<II", len(data), crc))
        ser.flush()
        read_until(ser, b"OK\n")

        off = 0
        while off < len(data):
            chunk = data[off:off + 256]
            ser.write(struct.pack("<H", len(chunk)) + chunk)
            ser.flush()
            ack = ser.read(1)
            if ack != b"\x06":
                raise RuntimeError(f"chunk {off:#x}: no ACK (got {ack!r})")
            off += len(chunk)
            print(f"\r{off}/{len(data)}", end="", flush=True)
        print()
        read_until(ser, b"DONE\n", timeout=10.0)
        print("flashed; radio resetting")


if __name__ == "__main__":
    main()
