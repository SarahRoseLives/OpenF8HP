#!/usr/bin/env python3
"""Verified backup of the BF-F8HP Pro config EEPROM (64 KiB).

Reads the whole 16-bit CPS address space twice, byte-for-byte, and only
writes a backup when both passes agree. Saves:

    eeprom_raw_64k.bin      exactly what came off the radio (still scrambled)
    eeprom_decoded_64k.bin  descrambled, human-readable codeplug view
    backup.json             metadata + SHA-256 of both files

The 4 KiB KDH bootloader and the application flash are not reachable over
this protocol; use SWD (or the vendor .kdhx) for those.

Usage:
    ./backup.py [--port /dev/ttyUSB0] [--out work/backups] [--passes 2]
"""
import argparse
import datetime
import hashlib
import json
import os
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
        self.ident = {}

    def identify(self):
        self.s.reset_input_buffer()
        self.s.write(MAGIC)
        self.s.flush()
        if _read_exact(self.s, 1) != b"\x06":
            return False
        for magic, n in EXTRA:
            self.s.write(magic)
            self.s.flush()
            r = _read_exact(self.s, n)
            if len(r) != n:
                return False
            self.ident[magic[:1].hex()] = r.hex()
        return True

    def read_raw_block(self, addr, retries=4):
        frame = b"R" + struct.pack(">H", addr) + bytes([BLOCK])
        for _ in range(retries):
            self.s.write(frame)
            self.s.flush()
            resp = _read_exact(self.s, BLOCK + 4, timeout=1.0)
            if (len(resp) == BLOCK + 4 and resp[0] == 0x52
                    and resp[1] == (addr >> 8) and resp[2] == (addr & 0xFF)
                    and resp[3] == BLOCK):
                return resp[4:]
        return None

    def read_all(self):
        out = bytearray()
        for addr in range(0, TOTAL, BLOCK):
            blk = self.read_raw_block(addr)
            if blk is None:
                raise RuntimeError(f"read failed at 0x{addr:04X}")
            out += blk
        return bytes(out)

    def close(self):
        self.s.close()


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="/dev/ttyUSB0")
    p.add_argument("--out", default="work/backups")
    p.add_argument("--passes", type=int, default=2)
    args = p.parse_args(argv)
    if serial is None:
        print("pyserial is required (pip install pyserial)", file=sys.stderr)
        return 2
    if args.passes < 2:
        print("refusing: need at least 2 passes to verify the backup",
              file=sys.stderr)
        return 2

    cps = Cps(args.port)
    if not cps.identify():
        cps.close()
        print("radio did not answer the CPS handshake "
              "(switch it on in normal mode)", file=sys.stderr)
        return 1

    passes = []
    for i in range(args.passes):
        print(f"pass {i + 1}/{args.passes} ...", flush=True)
        passes.append(cps.read_all())
        if i and passes[i] != passes[0]:
            bad = next(j for j in range(TOTAL) if passes[i][j] != passes[0][j])
            cps.close()
            print(f"ABORT: passes differ at 0x{bad:04X}; radio or cable is "
                  "unstable. Try again with the cable pressed in.",
                  file=sys.stderr)
            return 1
    cps.close()

    raw = passes[0]
    decoded = crypt(raw)
    ts = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    outdir = os.path.join(args.out, ts)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "eeprom_raw_64k.bin"), "wb") as f:
        f.write(raw)
    with open(os.path.join(outdir, "eeprom_decoded_64k.bin"), "wb") as f:
        f.write(decoded)

    meta = {
        "created_utc": ts,
        "port": args.port,
        "passes": args.passes,
        "size": len(raw),
        "sha256_raw": sha(raw),
        "sha256_decoded": sha(decoded),
        "cps_ident": cps.ident,
        "key": KEY.hex(),
        "address_space": "0x0000-0xFFFF",
    }
    with open(os.path.join(outdir, "backup.json"), "w") as f:
        json.dump(meta, f, indent=2)

    print(f"verified {args.passes} identical passes")
    print(f"raw     {meta['sha256_raw']}")
    print(f"decoded {meta['sha256_decoded']}")
    print(f"saved to {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
