#!/usr/bin/env python3
"""Flash a KDH `.kdhx` image to the BF-F8HP Pro bootloader.

The file is sent as-is (the bootloader descrambles it). Dry-run by default;
pass `--write --yes` to actually flash.

After the END command the radio reboots into the new app. With `--read`,
this tool reopens the port and prints whatever the app sends (useful for
the hello firmware, which prints over USART1).

Usage:
    ./flash_kdhx.py firmware/hello/hello.kdhx --dry-run
    ./flash_kdhx.py firmware/hello/hello.kdhx --write --yes --read
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    serial = None

import kdh_bootloader as kdh
import kdhx

PKG = 1024


def validate(path):
    raw = open(path, "rb").read()
    if len(raw) < 256:
        raise SystemExit("image too small")
    if len(raw) > 255 * PKG:
        raise SystemExit("image too large for the KDH chunk count")
    img = kdhx.decode(raw)
    sp, reset = struct.unpack_from("<II", img, 0)
    if not (0x20000000 <= sp <= 0x20010000):
        raise SystemExit(f"bad stack pointer 0x{sp:08X}")
    if not (0x08001000 <= reset < 0x08020000 and (reset & 1)):
        raise SystemExit(f"bad reset vector 0x{reset:08X}")
    chunks = (len(raw) + PKG - 1) // PKG
    return raw, img, sp, reset, chunks


def open_port(port):
    s = serial.Serial(port, 115200, bytesize=8, parity=serial.PARITY_NONE,
                      stopbits=serial.STOPBITS_ONE, timeout=0.2)
    s.dtr = True
    s.rts = True
    time.sleep(0.1)
    s.reset_input_buffer()
    s.reset_output_buffer()
    return s


def send(s, cmd, arg=0, data=b"", what="", timeout=5.0, retries=5):
    pkt = kdh.build_packet(cmd, arg, data)
    for attempt in range(retries):
        s.reset_input_buffer()
        s.write(pkt)
        s.flush()
        resp = kdh.read_frame(s, timeout)
        if resp is None:
            if attempt + 1 < retries:
                time.sleep(0.05)
                continue
            raise RuntimeError(f"{what}: no response")
        rcmd, rarg, _ = resp
        if rarg == kdh.ACK:
            return
        if rarg == 0xE2 and attempt + 1 < retries:
            continue
        raise RuntimeError(f"{what}: error 0x{rarg:02X}")


def flash(port, raw, chunks, read_after=False):
    s = open_port(port)
    try:
        send(s, kdh.CMD_HANDSHAKE, 0, b"BOOTLOADER", "handshake")
        print("handshake OK")
        send(s, kdh.CMD_UPDATE_DATA_PACKAGES, 0, bytes([chunks]),
             "announce")
        for i in range(chunks):
            block = raw[i * PKG:(i + 1) * PKG].ljust(PKG, b"\x00")
            send(s, kdh.CMD_UPDATE, i & 0xFF, block, f"chunk {i + 1}/{chunks}")
            print(f"\r  writing {i + 1}/{chunks}", end="", flush=True)
        print()
        send(s, kdh.CMD_UPDATE_END, 0, b"", "finalize")
        print("finalize OK")
    finally:
        s.close()

    if read_after:
        time.sleep(0.5)
        with open_port(port) as s:
            print("--- app output (Ctrl-C to stop) ---")
            deadline = time.time() + 8.0
            try:
                while time.time() < deadline:
                    data = s.read(256)
                    if data:
                        sys.stdout.write(data.decode("ascii", "replace"))
                        sys.stdout.flush()
            except KeyboardInterrupt:
                pass


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("file")
    p.add_argument("--port", default="/dev/ttyUSB0")
    p.add_argument("--write", action="store_true")
    p.add_argument("--yes", action="store_true")
    p.add_argument("--read", action="store_true")
    args = p.parse_args(argv)
    if serial is None:
        print("pyserial is required", file=sys.stderr)
        return 2
    raw, img, sp, reset, chunks = validate(args.file)
    print(f"{args.file}: {len(raw)} bytes, {chunks} chunks")
    print(f"  SP=0x{sp:08X} reset=0x{reset:08X}")
    if not args.write:
        print("dry run only; add --write --yes to flash")
        return 0
    if not args.yes:
        print("refusing: pass --yes with --write", file=sys.stderr)
        return 2
    flash(args.port, raw, chunks, read_after=args.read)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
