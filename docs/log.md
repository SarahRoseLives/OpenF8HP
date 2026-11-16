# Research log

Chronological record of findings. Newest entries at the bottom.

## 2026-11-16

### Initial read / firmware access

- Confirmed `/dev/ttyUSB0` = FTDI FT232R programming cable.
- Radio in bootloader mode (SK1+SK2 at power-on) answers the KDH handshake.
- Probed 34 non-update opcodes; all returned `0xE5`. No serial flash-read
  command.
- Downloaded the official V0.53 bundle; SHA-256 matches the FlintWave
  manifest (`511f2a6b…be1d`).
- Identified `NRF` / `NRFB` images and the `.kdhx` scramble
  (`KDHT` / `RBGI` per 1 KiB block, first/last 2 KiB plaintext).
- Decoded both images; discovered the app load base is **`0x08001000`**
  (first 4 KiB = KDH bootloader), not `0x08000000`.

### MCU identification

- 92-entry vector table → 76 IRQs → AT32F413/415.
- SP `0x200078e0` + init table → 32 KiB SRAM.
- STM32F1-style peripheral map, SPI1, ADC1, DMA1/2, USART1/2/3 + UART4.

### CPS / EEPROM

- Normal-mode handshake `PROGRAMBF5RTECHU` → `"5RTECH"`.
- Read command has no bounds check; dumped all 64 KiB.
- Found and fixed the codeplug decryption bug: key `" EIY"`, skip positions
  where the key byte is a space. The first dump had corrupted names/digits.
- Verified backup: two identical 64 KiB passes.
  `work/backups/20261116T074009Z/` (raw + decoded + vendor bundle + manifest).

### RF

- `NRF` / `NRFB` = **FD6818 / FD6818B**.
- Recovered the full init sequence at `0x0800E604`.
- Bus pins: **SCN=PB0, SCK=PC5, SDA=PC4**.
- State register `0x30`: RX `0xBFF1`, TX `0xC1FE`.

### Display

- **ST7735**, 8-bit 8080 parallel.
- D0–D7 = PB8–PB15, CS=PB6, RST=PB7, RS=PC12, WR=PF6.
- Init at `0x080169F4`; 160 px horizontal clamp.

### Keypad

- Scan at `0x08009BD8`; rows driven on **PB8–PB12**, columns read on
  **PB2–PB5**.
- Side keys on PB2/PB3 with no row driven.
- Key table at `0x08016EF0`.
- Inferred label layout (unconfirmed; needs a press test):
  `Menu/Up/Down/Exit` on PB12, `1/2/3` on PB8, `4/5/6/V/M` on PB9,
  `7/8/9/A/B` on PB10, `*/0/#/Band` on PB11, side keys on PB2/PB3.
- Important: PB8–PB12 are shared with LCD D0–D4; scanning and display writes
  must be serialized.

### PTT / power

- PTT = **PA10, active low**, shared with USART1_RX (matches UV-5RH).
  Debounce at `0x0800DF28`; release wait at `0x080140FC`.
- Power key = **PC0**; power-down path at `0x080036E0`.

### Backups / tooling

- Added `tools/backup.py`, `tools/restore.py`, `tools/read_eeprom.py`,
  `tools/kdhx.py`, `tools/kdh_bootloader.py`.
- Backups and decoded firmware are under git-ignored `work/`.

### Hello image

- Added `firmware/hello/` — a 4 KiB bare-metal image linked at `0x08001000`.
- It sets HSI 8 MHz, USART1 PA9 at 115200, holds the LCD bus idle, scans the
  keypad matrix and PTT, and prints key codes over the programming cable.
- Validated the image: SP `0x200078E0`, reset `0x08001041`, 4 KDHX chunks.
- Added `tools/flash_kdhx.py` (dry-run by default, `--write --yes` to flash,
  `--read` to capture the app output afterwards).
- Built with clang/lld (`-Os`; `-O2` dropped a string in this toolchain).
- **Flashed successfully** via the KDH bootloader (4 chunks, END accepted).
  The app printed the banner and 2-second heartbeat dots on USART1
  PA9 @115200, proving the startup/base-address path and the UART pins.
- Key press test (over the cable) confirmed the matrix and labels:
  `1=0x02, 2=0x03, 4=0x05 … 9=0x0b, *=0x0d, 0=0x0e, #=0x0f,
  Menu=0x11, Up=0x12, Down=0x13, Exit=0x14, V/M=0x08, A/B=0x0c,
  Band=0x10`, top side `0x01` (SK1), bottom side `0x00` (SK2).
  `3=0x04` follows from the table; it was the only key not directly observed.
- **PTT = PA10 active low confirmed** end-to-end (`ptt=1`/`ptt=0`), even with
  the programming cable connected.
- Extended hello to the LCD: ST7735 parallel init, backlight PA4, `HELLO`
  and a live `KEY XX` line. Built as an 8 KiB / 8-chunk image and flashed.
- **LCD confirmed on hardware:** white `HELLO` on dark blue, upright,
  `KEY XX` updates on press, backlight on, PTT red square works.
- The radio currently runs the LCD hello image. To restore stock, flash the
  vendor V0.53 `.kdhx` from the backup.

### Next

- Flash the hello image and capture the UART/keypad output.
- Confirm key labels/order with a press test (or PCB inspection).
- Trace remaining unassigned pins (PB1, PC7, PA4/11–14, PC1–11/13–15,
  PD2, PF4/5/7).
- Identify the exact AT32 part and whether flash RDP is enabled (SWD would
  answer both).
