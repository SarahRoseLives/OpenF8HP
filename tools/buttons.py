#!/usr/bin/env python3
"""Control every BF-F8HP Pro button over the running firmware's debug UART.

The app accepts:
  k <code> <0|1>    hold/release one virtual button
  K <hexmask>       set the whole virtual button mask

All keypad keys, side keys, power, and PTT are mapped. Holding PTT transmits
until it is released (or the firmware's 60 s TX timeout fires), so use `up`.

Examples:
  python3 tools/buttons.py list
  python3 tools/buttons.py tap sk1
  python3 tools/buttons.py down ptt
  python3 tools/buttons.py up ptt
  python3 tools/buttons.py tap menu up down
  python3 tools/buttons.py mask 00400002
  python3 tools/buttons.py state
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: python3 -m pip install pyserial")


KEYS = {
    "sk2": 0x00,
    "sk1": 0x01,
    "1": 0x02,
    "2": 0x03,
    "3": 0x04,
    "4": 0x05,
    "5": 0x06,
    "6": 0x07,
    "v/m": 0x08,
    "7": 0x09,
    "8": 0x0A,
    "9": 0x0B,
    "a/b": 0x0C,
    "*": 0x0D,
    "0": 0x0E,
    "#": 0x0F,
    "band": 0x10,
    "menu": 0x11,
    "up": 0x12,
    "down": 0x13,
    "exit": 0x14,
    "power": 0x15,
    "ptt": 0x16,
}

ALIASES = {
    "side1": "sk1",
    "top": "sk1",
    "side2": "sk2",
    "bottom": "sk2",
    "vm": "v/m",
    "v/m": "v/m",
    "ab": "a/b",
    "a/b": "a/b",
    "band": "band",
}


def resolve(name: str) -> int:
    key = name.strip().lower()
    key = ALIASES.get(key, key)
    if key in KEYS:
        return KEYS[key]
    if key.startswith("0x"):
        code = int(key, 16)
        if 0 <= code <= 0x16:
            return code
    raise SystemExit(f"unknown button {name!r}; run `python3 {sys.argv[0]} list`")


def command(ser: serial.Serial, line: str, timeout: float) -> str:
    ser.reset_input_buffer()
    # The app's polled command parser needs a gap between bytes; a burst can
    # overrun the single-byte UART receive register.
    for b in (line + "\n").encode():
        ser.write(bytes((b,)))
        ser.flush()
        time.sleep(0.002)
    deadline = time.monotonic() + timeout
    buf = b""
    lines: list[str] = []
    while time.monotonic() < deadline:
        chunk = ser.read(1)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = raw.decode(errors="replace").strip()
            if not text:
                continue
            lines.append(text)
            if text.startswith("keymask=") or text.startswith("bad key") or text.startswith("usage:"):
                return text
    if lines:
        return lines[-1]
    raise TimeoutError(f"no response to {line!r} (is the app running?)")


def send_mask(ser: serial.Serial, mask: int, timeout: float) -> str:
    return command(ser, f"K {mask:08x}", timeout)


def send_key(ser: serial.Serial, code: int, state: int, timeout: float) -> str:
    return send_mask(ser, (1 << code) if state else 0, timeout)


def main() -> None:
    ap = argparse.ArgumentParser(description="Control BF-F8HP Pro buttons over UART")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("--ms", type=int, default=180, help="tap hold time (default 180)")
    ap.add_argument(
        "action",
        choices=["list", "tap", "down", "up", "clear", "state", "mask"],
        help="button action",
    )
    ap.add_argument("buttons", nargs="*", help="button names or 0xNN codes")
    args = ap.parse_args()

    if args.action == "list":
        for name, code in KEYS.items():
            print(f"{name:5s} code=0x{code:02x} bit={code:2d} mask=0x{1 << code:08x}")
        return

    if args.action == "mask":
        if len(args.buttons) != 1:
            raise SystemExit("mask needs one hex value, e.g. 00400002")
        mask = int(args.buttons[0], 16) & 0x7FFFFF
        with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
            time.sleep(0.2)
            print(send_mask(ser, mask, args.timeout))
        return

    if args.action == "clear":
        with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
            time.sleep(0.2)
            print(send_mask(ser, 0, args.timeout))
        return

    if args.action == "state":
        with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
            time.sleep(0.2)
            print(command(ser, "K", args.timeout))
        return

    if not args.buttons:
        raise SystemExit(f"{args.action} needs at least one button")

    codes = [resolve(name) for name in args.buttons]
    with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
        time.sleep(0.2)
        result = ""
        if args.action == "tap":
            for code in codes:
                result = send_key(ser, code, 1, args.timeout)
            time.sleep(max(0.0, args.ms / 1000.0))
            for code in codes:
                result = send_key(ser, code, 0, args.timeout)
        else:
            state = 1 if args.action == "down" else 0
            for code in codes:
                result = send_key(ser, code, state, args.timeout)
        print(result)

    if args.action == "down" and 0x16 in codes:
        print("PTT held: run `up ptt` or `clear` to stop transmitting", file=sys.stderr)


if __name__ == "__main__":
    main()
