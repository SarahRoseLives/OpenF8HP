# OpenF8HP

Open-source firmware research for the **BTECH BF-F8HP Pro** (Baofeng `H802`,
NRF / NRFB hardware variants).

This repository is currently in the **reverse-engineering / bring-up** phase.
It contains the tools and notes needed to back up the radio, decode vendor
firmware, and start building a replacement image that can be flashed with the
stock KDH bootloader.

## Hardware at a glance

| Item | Finding |
| --- | --- |
| MCU | AT32F413 or AT32F415 (Cortex-M4F), 32 KiB SRAM, 128 KiB flash (likely) |
| Firmware layout | 4 KiB KDH bootloader at `0x08000000`; app at `0x08001000` |
| RF | FD6818 (`NRF`) / FD6818B (`NRFB`), 3-wire bus |
| RF bus | SCN `PB0`, SCK `PC5`, SDA `PC4` |
| Display | ST7735, 8-bit 8080 parallel bus, 160 px wide |
| LCD bus | D0–D7 `PB8–PB15`, CS `PB6`, RST `PB7`, RS `PC12`, WR `PF6`, backlight `PA4` |
| Keypad | 5 rows `PB8–PB12`, 4 columns `PB2–PB5` (rows share LCD D0–D4) |
| PTT | `PA10`, active low (shared with USART1_RX) |
| Power key | `PC0` |
| Config memory | 64 KiB EEPROM/address space over the CPS protocol |

See [docs/hardware.md](docs/hardware.md) for the full pin map.

## Tools

| Tool | Purpose |
| --- | --- |
| `tools/backup.py` | Verified 64 KiB EEPROM backup (reads twice, compares) |
| `tools/restore.py` | Guarded write-back of a backup (dry-run by default) |
| `tools/read_eeprom.py` | Read the codeplug map or the full 64 KiB space |
| `tools/kdhx.py` | Decode/encode vendor `.kdhx` firmware containers |
| `tools/kdh_bootloader.py` | KDH handshake and opcode probe |
| `tools/flash_kdhx.py` | Flash a `.kdhx` image through the KDH bootloader |

## Firmware

**Current milestone:** the `hello` bring-up image runs on real hardware —
USART1 output, ST7735 `HELLO` + live key code, full keypad map, and PTT.

- `firmware/hello/` — bring-up image linked at `0x08001000`.
  - ST7735 parallel LCD, backlight `PA4`
  - keypad matrix rows `PB8–PB12`, columns `PB2–PB5`
  - PTT `PA10` active low
  - USART1 `PA9` @ 115200 for diagnostics
  - build with `make`, flash with `tools/flash_kdhx.py`
  See [firmware/hello/README.md](firmware/hello/README.md).

Backups, decoded firmware and extracted vendor files live under `work/`,
which is git-ignored.

## Quick start

```sh
python3 -m pip install pyserial
python3 tools/backup.py                 # verified EEPROM backup
python3 tools/kdhx.py file.kdhx out.bin # decode vendor firmware
```

## Documentation

- [docs/hardware.md](docs/hardware.md) — MCU, pin map, RF, LCD, keypad, PTT
- [docs/firmware.md](docs/firmware.md) — `.kdhx` container, memory layout, init code
- [docs/protocols.md](docs/protocols.md) — KDH bootloader, CPS protocol, EEPROM map
- [docs/log.md](docs/log.md) — chronological research log

## Safety

- Keep a verified backup before writing anything to the radio.
- The 4 KiB bootloader is **not** dumped yet. Do not attempt to disable flash
  read protection without a bootloader image and a recovery plan.
- The vendor `.kdhx` files can restore the application image; they do not
  restore the bootloader.
