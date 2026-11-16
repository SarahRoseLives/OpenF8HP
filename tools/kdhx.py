#!/usr/bin/env python3
"""Encode/decode KDH `.kdhx` firmware containers.

Scheme (reverse-engineered by OK2MOP / amoxu for the Baofeng UV-5RM family,
and byte-identical for the BTECH BF-F8HP Pro):

  * The payload is processed in 1024-byte packages.
  * The first two and last two packages are stored in plaintext.
  * In between, package i is XORed with a 4-byte key:
        i % 3 == 1 -> b"KDHT"
        i % 3 == 2 -> b"RBGI"
        i % 3 == 0 -> plaintext
  * A byte is *not* transformed if it is 0x00, 0xFF, key[k], or key[k] ^ 0xFF.

The transform is an involution, so decode() and encode() are the same thing.
"""
import sys

PKG = 1024
KEY1 = b"KDHT"
KEY2 = b"RBGI"


def xor_crypt(data: bytes, key: bytes) -> bytes:
    out = bytearray(data)
    for i, b in enumerate(out):
        k = key[i % 4]
        if b not in (0x00, 0xFF, k, k ^ 0xFF):
            out[i] = b ^ k
    return bytes(out)


def decode(data: bytes) -> bytes:
    n = len(data)
    pkgs = (n + PKG - 1) // PKG
    out = bytearray(data)
    for i in range(2, pkgs - 2):
        key = KEY1 if i % 3 == 1 else KEY2 if i % 3 == 2 else None
        if key is None:
            continue
        off = i * PKG
        blk = data[off:off + PKG]
        out[off:off + len(blk)] = xor_crypt(blk, key)
    return bytes(out)


encode = decode


def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} <in.kdhx|in.bin> <out.bin|out.kdhx>",
              file=sys.stderr)
        return 2
    raw = open(argv[1], "rb").read()
    out = decode(raw)
    open(argv[2], "wb").write(out)
    print(f"{argv[1]}: {len(raw)} bytes -> {argv[2]} ({len(out)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
