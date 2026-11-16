#!/usr/bin/env python3
"""Restore a BF-F8HP Pro config EEPROM backup over the CPS protocol.

Source file: either `eeprom_raw_64k.bin` (preferred, byte-exact) or
`eeprom_decoded_64k.bin`. Both are 64 KiB with addresses in place; the
scrambling transform is an involution, so the tool accepts either.

Safety:
  * dry-run by default: prints the frames it would send, writes nothing
  * `--write --yes` is required to touch the radio
  * after writing, it re-reads every written block and compares
  * only the writable CHIRP regions are touched unless `--full` is given

Usage:
    ./restore.py work/backups/<ts>/eeprom_raw_64k.bin
    ./restore.py --write --yes work/backups/<ts>/eeprom_raw_64k.bin
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
TOTAL = 0x10000
# BF-F8HP-PRO writable map (CHIRP F8HPPro.MEM_STARTS / MEM_SIZES)
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


class Cps:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, bytesize=8,
                               parity=serial.PARITY_NONE,
                               stopbits=serial.STOPBITS_ONE, timeout=1.5)
        self.s.dtr = True
        self.s.rts = True
        time.sleep(0.2)
        self.s.reset_input_buffer()
        self.s.reset_output_buffer()

    def identify(self):
        self.s.reset_input_buffer()
        self.s.write(MAGIC)
        self.s.flush()
        if _read_exact(self.s, 1) != b"\x06":
            return False
        for magic, n in EXTRA:
            self.s.write(magic)
            self.s.flush()
            if len(_read_exact(self.s, n)) != n:
                return False
        return True

    def read_block(self, addr):
        self.s.write(b"R" + struct.pack(">H", addr) + bytes([BLOCK]))
        self.s.flush()
        resp = _read_exact(self.s, BLOCK + 4, timeout=1.0)
        if (len(resp) == BLOCK + 4 and resp[0] == 0x52
                and resp[1] == (addr >> 8) and resp[2] == (addr & 0xFF)):
            return resp[4:]
        return None

    def write_block(self, addr, raw):
        frame = (b"W" + struct.pack(">H", addr) + bytes([len(raw)])
                 + crypt(raw))
        for _ in range(4):
            self.s.write(frame)
            self.s.flush()
            if _read_exact(self.s, 1, timeout=1.0) == b"\x06":
                return True
        return False

    def close(self):
        self.s.close()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("file")
    p.add_argument("--port", default="/dev/ttyUSB0")
    p.add_argument("--full", action="store_true",
                   help="write all 64 KiB (default: only the CHIRP map)")
    p.add_argument("--write", action="store_true", help="actually write")
    p.add_argument("--yes", action="store_true", help="confirm the write")
    args = p.parse_args(argv)
    if serial is None:
        print("pyserial is required (pip install pyserial)", file=sys.stderr)
        return 2
    data = open(args.file, "rb").read()
    if len(data) != TOTAL:
        print(f"refusing: {args.file} is {len(data)} bytes, expected {TOTAL}",
              file=sys.stderr)
        return 2
    regions = [(0, TOTAL)] if args.full else MAP
    if not args.write:
        for start, size in regions:
            print(f"would write 0x{start:04X}..0x{start + size:04X} "
                  f"({size} bytes)")
        print("dry run only; add --write --yes to restore")
        return 0
    if not args.yes:
        print("refusing: pass --yes with --write", file=sys.stderr)
        return 2

    cps = Cps(args.port)
    if not cps.identify():
        cps.close()
        print("radio did not answer the CPS handshake (normal mode)",
              file=sys.stderr)
        return 1
    total = 0
    for start, size in regions:
        for addr in range(start, start + size, BLOCK):
            if not cps.write_block(addr, data[addr:addr + BLOCK]):
                cps.close()
                print(f"write failed at 0x{addr:04X}", file=sys.stderr)
                return 1
            total += BLOCK
        print(f"wrote 0x{start:04X}..0x{start + size:04X}")
    cps.close()
    print(f"wrote {total} bytes; verifying ...")

    cps = Cps(args.port)
    if not cps.identify():
        cps.close()
        print("verification handshake failed", file=sys.stderr)
        return 1
    for start, size in regions:
        for addr in range(start, start + size, BLOCK):
            got = cps.read_block(addr)
            if got != data[addr:addr + BLOCK]:
                cps.close()
                print(f"verify mismatch at 0x{addr:04X}", file=sys.stderr)
                return 1
    cps.close()
    print("verify OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
