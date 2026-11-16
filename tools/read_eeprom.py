#!/usr/bin/env python3
"""Read the BF-F8HP Pro's 64 KiB config EEPROM over the CPS serial protocol.

Handshake (115200 8N1) is `PROGRAMBF5RTECHU`, then a few follow-up magics,
exactly as CHIRP's `chirp/drivers/baofeng_uv17Pro.py` (class F8HPPro) does.

Read frame:  b"R" + big-endian u16 address + u8 length
Reply:       4-byte header (0x52, addrH, addrL, length) + data

Each 0x40-byte data block is XOR-scrambled with the codeplug key b" EIY",
using the same skip rule as the firmware container (leave 0x00, 0xFF, key,
and key ^ 0xFF untouched). Positions whose key byte is a space (0x20) are
not transformed at all, matching CHIRP's `_crypt()`.

Address map (from CHIRP):
    0x0000  channels (1000 entries, 64 bytes each)
    0x8000  settings
    0x9000  scan lists / extra
    0xA000  contact list
    0xB000  call/zone-channel entries
    0xD000  zone names
    0xF000  calibration-ish tables

Usage:
    ./read_eeprom.py [--port /dev/ttyUSB0] [--full] [out.bin]
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    serial = None

MAGIC = b"PROGRAMBF5RTECHU"
EXTRA = [
    (b"\x46", 16),
    (b"\x4d", 6),
    (b"\x53\x45\x4e\x44\x12\x0d\x0a\x0a\x10\x03\x0d\x02\x11\x0c"
     b"\x12\x0a\x11\x06\x04\x0e\x02\x09\x0d\x00\x00", 1),
]
KEY = b" EIY"
BLOCK = 0x40

# CHIRP's memory map for the F8HPPro
MAP = [(0x0000, 0x8040), (0x9000, 0x0080), (0xA000, 0x02C0), (0xD000, 0x00C0)]


def crypt(data: bytes, key: bytes = KEY) -> bytes:
    out = bytearray(data)
    for i, b in enumerate(out):
        k = key[i % 4]
        if k != 0x20 and b not in (0x00, 0xFF, k, k ^ 0xFF):
            out[i] = b ^ k
    return bytes(out)


def _read_exact(s, n, timeout=1.5):
    s.timeout = timeout
    buf = b""
    while len(buf) < n:
        chunk = s.read(n - len(buf))
        if not chunk:
            break
        buf += chunk
    return buf


def identify(s):
    s.reset_input_buffer()
    s.write(MAGIC)
    s.flush()
    if _read_exact(s, 1) != b"\x06":
        return False
    for magic, n in EXTRA:
        s.write(magic)
        s.flush()
        if len(_read_exact(s, n)) != n:
            return False
    return True


def read_block(s, addr, length=BLOCK, retries=3):
    frame = b"R" + struct.pack(">H", addr) + bytes([length])
    for _ in range(retries):
        s.write(frame)
        s.flush()
        resp = _read_exact(s, length + 4, timeout=1.0)
        if len(resp) == length + 4 and resp[0] == 0x52:
            return crypt(resp[4:])
    return None


def dump(port, full=False, progress=True):
    if serial is None:
        raise RuntimeError("pyserial is required (pip install pyserial)")
    s = serial.Serial(port, 115200, bytesize=8, parity=serial.PARITY_NONE,
                      stopbits=serial.STOPBITS_ONE, timeout=1.5)
    s.dtr = True
    s.rts = True
    time.sleep(0.2)
    s.reset_input_buffer()
    s.reset_output_buffer()
    if not identify(s):
        s.close()
        raise RuntimeError("radio did not answer the CPS handshake "
                           "(is it on in normal mode?)")

    out = bytearray()
    regions = [(0x0000, 0x10000)] if full else MAP
    for start, size in regions:
        for addr in range(start, start + size, BLOCK):
            data = read_block(s, addr)
            if data is None:
                s.close()
                raise RuntimeError(f"read failed at 0x{addr:04X}")
            out += data
            if progress and addr % 0x1000 == 0:
                print(f"\r  reading 0x{addr:04X}", end="", flush=True)
    s.close()
    if progress:
        print("\r  done            ")
    return bytes(out)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="/dev/ttyUSB0")
    p.add_argument("--full", action="store_true",
                   help="dump all 64 KiB instead of the CHIRP codeplug map")
    p.add_argument("out", nargs="?", default=None)
    args = p.parse_args(argv)
    out = args.out or ("eeprom.bin" if args.full else "codeplug.bin")
    data = dump(args.port, full=args.full)
    open(out, "wb").write(data)
    print(f"wrote {out}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
