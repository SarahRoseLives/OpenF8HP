#!/usr/bin/env python3
"""Convert a small binary blob to a C byte array."""
import sys
from pathlib import Path

src, dst, name, length_name = sys.argv[1:5]
data = Path(src).read_bytes()
with open(dst, "w", encoding="ascii") as f:
    f.write("#include <stdint.h>\n\n")
    f.write(f"const uint8_t {name}[] = {{\n")
    for i in range(0, len(data), 12):
        f.write("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",\n")
    f.write("};\n\n")
    f.write(f"const uint32_t {length_name} = {len(data)}u;\n")
