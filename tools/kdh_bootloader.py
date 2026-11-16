#!/usr/bin/env python3
"""Minimal KDH bootloader tool for the BF-F8HP Pro (and siblings).

Packet format (115200 8N1):
    [0xAA][cmd][arg][lenH][lenL][data...][crcH][crcL][0xEF]
CRC-16/CCITT (poly 0x1021, init 0x0000) over cmd+arg+len+data.

Commands used by the official updater:
    0x01  hello        payload b"BOOTLOADER"
    0x04  count        one byte: number of 1024-byte chunks
    0x03  data         arg = chunk index, 1024-byte payload
    0x42  enter boot   (replied 0xE5 when already in the bootloader)
    0x45  end          commit and reboot

No read/backup command was found: all 34 tested opcodes outside the update
set answered 0xE5. Dumping the bootloader itself will need SWD.

Usage:
    ./kdh_bootloader.py handshake [port]
    ./kdh_bootloader.py diag [port]
"""
import sys
import time

try:
    import serial
except ImportError:
    serial = None

CMD_HANDSHAKE = 0x01
CMD_UPDATE = 0x03
CMD_UPDATE_DATA_PACKAGES = 0x04
CMD_INTO_BOOT = 0x42
CMD_UPDATE_END = 0x45

HEADER = 0xAA
TRAILER = 0xEF
ACK = 0x06

ERRORS = {
    0xE1: "handshake rejected",
    0xE2: "data verification error (retryable)",
    0xE3: "incorrect address",
    0xE4: "flash write error",
    0xE5: "command error",
}


def crc16_ccitt(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 \
                else (crc << 1) & 0xFFFF
    return crc


def build_packet(cmd: int, arg: int = 0, data: bytes = b"") -> bytes:
    body = bytes([cmd, arg, (len(data) >> 8) & 0xFF, len(data) & 0xFF]) + data
    crc = crc16_ccitt(body)
    return bytes([HEADER]) + body + bytes([crc >> 8, crc & 0xFF, TRAILER])


def read_frame(ser, timeout=1.5):
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if not buf:
            if b[0] != HEADER:
                continue
            buf += b
            continue
        buf += b
        if len(buf) == 5:
            need = 8 + ((buf[3] << 8) | buf[4])
        if len(buf) >= 8 and len(buf) == need:
            break
    if len(buf) < 8 or buf[-1] != TRAILER:
        return None
    n = (buf[3] << 8) | buf[4]
    if crc16_ccitt(bytes(buf[1:5 + n])) != ((buf[5 + n] << 8) | buf[6 + n]):
        raise ValueError("bad CRC")
    return buf[1], buf[2], bytes(buf[5:5 + n])


def exchange(ser, cmd, arg=0, data=b"", timeout=1.5):
    ser.reset_input_buffer()
    ser.write(build_packet(cmd, arg, data))
    ser.flush()
    resp = read_frame(ser, timeout)
    if resp is None:
        return None
    rcmd, rarg, rdata = resp
    if rarg == ACK:
        return rcmd, rarg, rdata
    raise RuntimeError(f"cmd 0x{cmd:02X}: {ERRORS.get(rarg, hex(rarg))}")


def open_port(port):
    s = serial.Serial(port, 115200, bytesize=8, parity=serial.PARITY_NONE,
                      stopbits=serial.STOPBITS_ONE, timeout=0.2)
    s.dtr = True
    s.rts = True
    time.sleep(0.1)
    s.reset_input_buffer()
    s.reset_output_buffer()
    return s


def cmd_handshake(port):
    with open_port(port) as s:
        r = exchange(s, CMD_HANDSHAKE, 0, b"BOOTLOADER")
        print("handshake:", "OK" if r else "no response")


def cmd_diag(port):
    probes = [0x00, 0x02, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
              0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x20, 0x22, 0x30, 0x40, 0x43,
              0x44, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x50, 0x52, 0x55, 0xFF]
    with open_port(port) as s:
        print("handshake:", "OK" if exchange(s, CMD_HANDSHAKE, 0,
                                             b"BOOTLOADER") else "fail")
        for cmd in probes:
            s.reset_input_buffer()
            s.write(build_packet(cmd))
            s.flush()
            try:
                r = read_frame(s, 0.5)
            except ValueError as e:
                print(f"  0x{cmd:02X}: {e}")
                continue
            print(f"  0x{cmd:02X}: " + (f"cmd=0x{r[0]:02X} arg=0x{r[1]:02X} "
                                        f"len={len(r[2])}" if r else "no reply"))
            exchange(s, CMD_HANDSHAKE, 0, b"BOOTLOADER")


def main(argv):
    if serial is None:
        print("pyserial is required (pip install pyserial)", file=sys.stderr)
        return 2
    mode = argv[1] if len(argv) > 1 else "handshake"
    port = argv[2] if len(argv) > 2 else "/dev/ttyUSB0"
    if mode == "handshake":
        cmd_handshake(port)
    elif mode == "diag":
        cmd_diag(port)
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
