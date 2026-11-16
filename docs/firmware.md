# Firmware notes — BF-F8HP Pro V0.53

## Vendor bundle

The official BTECH V0.53 bundle is
`F8HPPRO-V53-Update-Bundle.zip` with SHA-256
`511f2a6b7df51c9dc4297728f4fc374359d6a23f806627bd4209e239048fbe1d`
(matches `flintwave-kdh-flasher`'s `firmware_manifest.json`).

It contains two application images:

| File | Version string | Size |
| --- | --- | --- |
| `NRF_ONLY_BF-F8HP-PRO_FIRMWARE_V0.53.kdhx` | `F8HP PRO NRF` / `V0.53` | 108484 |
| `NRFB_ONLY_BF-F8HP-PRO_FIRMWARE_V0.53.kdhx` | `F8HPPRO NRFB-YP07` / `V0.53` | 108976 |

The `.kdhx` files are scrambled application images, not raw flash dumps.

## `.kdhx` container

Algorithm (reverse-engineered by OK2MOP / amoxu, identical here):

- Process in **1024-byte packages**.
- The **first two** and **last two** packages are plaintext.
- For package index `i` in the encrypted range:
  - `i % 3 == 1` → XOR with `b"KDHT"`
  - `i % 3 == 2` → XOR with `b"RBGI"`
  - `i % 3 == 0` → plaintext
- Per byte, with key byte `k = key[i % 4]`, skip the XOR if the byte is
  `0x00`, `0xFF`, `k`, or `k ^ 0xFF`.

The transform is an involution; `tools/kdhx.py` encodes and decodes.

## Load address and layout

The decoded image is linked at **`0x08001000`**, not `0x08000000`.
The first 4 KiB of flash is the KDH bootloader.

| Image | App range |
| --- | --- |
| NRF | `0x08001000–0x0801B7C3` |
| NRFB | `0x08001000–0x0801B9AF` |

The startup chain:

```
vector[1] = 0x08001185        (app vector table at 0x08001000)
0x08001184: ldr r0, =SystemInit (0x08012DC0)
            blx r0
0x08001188: ldr r0, =0x08001171
            bx r0
0x08001170: ldr.w sp, =0x200078E0
            bl  0x0800197C        ; C runtime init-array walker
            ldr r0, =0x08015C51   ; main
            bx  r0
```

- `SystemInit` is at **`0x08012DC0`** (CRM/PWR/FLASH setup).
- The init-array table lives at **`0x0801B50C`** (two 16-byte descriptors).
- The app’s top-level entry is `0x08015C50`.

## Vector table

92 entries = 16 system + **76 IRQs** → AT32F413/415.

Non-default IRQ handlers:

| IRQ | Vector address | Use |
| --- | --- | --- |
| 15 | `0x08004031` | DMA1 channel 5 |
| 37 | `0x080134C9` | USART1 |
| 38 | `0x080134F1` | USART2 |
| 39 | `0x080134F5` | USART3 |
| 52 | `0x080134C5` | UART4 |

## FD6818 init

`0x0800E604` performs the RF init. Register writes use the bit-banged
`write_reg` at `0x0800FD40`, which drives **SCN=PB0, SCK=PC5, SDA=PC4**.

Recovered sequence (register ← value):

```
0x00 ← 0x8000   soft reset
0x00 ← 0x0000
0x37 ← 0x1D0F   power up
0x13 ← 0x03BE
0x12 ← 0x037B
0x11 ← 0x027B
0x10 ← 0x007A
0x14 ← 0x0019
0x49 ← 0x2A38
0x7B ← 0x8420
0x19 ← 0x1041
0x2A ← 0x4F18
0x09 ← DTMF coefficient table
       (0x006F, 0x106B, 0x2067, 0x3062, 0x4050, 0x5047, 0x603A,
        0x702C, 0x8041, 0x9037, 0xA025, 0xB017, 0xC0E4, 0xD0CB,
        0xE0B5, 0xF09F)
0x21 ← 0x06D8
0x5C ← 0x5665
0x5D ← 0x0F00
0x40 ← read-modify-write, deviation value 0x04E0
0x31 ← clear bit 1
0x7D ← mic sensitivity
0x48 ← volume
0x1F ← 0x5454
0x3E ← 0xA037
0x77 ← 0x88EF
0x4F ← 0x3732
0x26 ← 0x13A0
```

The FD6818 state function is `0x0800F998(state)` with
`state 1 = RX (0xBFF1)`, `state 2 = TX (0xC1FE)`, `state 3 = tone`,
`state 4 = TX tone (0xC3FA)`.

Bit-bang edge order (from `0x0800F4EC`): **SCK low → SDA → delay → SCK high
→ delay**, MSB first. The reverse order still gets some replies but returns
wrong register values (observed: chip ID `0x34D0` instead of `0x4819`).

Confirmed on hardware with the hello image: chip ID `0x4819`, RX at
**446.000 MHz** (frequency word `0x02A88AC0`), and RSSI `0x67` responds to a
keyed 446.000 signal (baseline ~31–33 → ~111).

Audio path confirmed on hardware:

- speaker amp enable **PC14 active high**
- `0x47 = 0x6140` (AF out, RX audio)
- `0x48 = 0x83FF` (max test volume; vendor AF path uses
  `0x800F | (cal << 4)`)
- `0x78 = 0x0000` (squelch open for the test)
- AF response: `0x74=0xFEF0`, `0x44=0x8D8F`, `0x45=0x3359`
- 446.000 MHz voice was audible.

## ST7735 init

`0x080169F4` resets the panel (PB7) then writes the ST7735 sequence:

`0x11` (sleep out), `0xB1`, `0xB2`, `0xB3`, `0xB4`, `0xC0`, `0xC1`,
`0xC2`, `0xC3`, `0xC4`, `0xC5`, gamma `0xE0`/`0xE1`, vendor `0xFC ← 0x8C`,
`0x36 ← 0x60` (MADCTL), `0x3A ← 0x05` (16-bit color), `0x29` (display on).

This is the classic ST7735 family sequence plus a vendor-specific `0xFC`
command; treat the controller as ST7735-compatible.

Window and pixel helpers: `0x08016890` (0x2A/0x2B), `0x0801683C`
(command), `0x080168C0` (data). The driver clamps X to `0xA0` = 160 px.
Backlight is **PA4 active high** (`0x0800B20C`).

The `firmware/hello` image reproduces this init and was confirmed on hardware:
the panel shows `446.000`, `ID 4819`, `RSSI`, and a live key code in the
correct orientation.

## Keypad

Scan routine: **`0x08009BD8`**. Key table: **`0x08016EF0`** (24 bytes).
See `docs/hardware.md` for the matrix, codes and inferred labels.

## CPS / codeplug code

- CPS handshake string: `PROGRAMBF5RTECHU` (also in the app image).
- Read frame: `'R' + BE16 address + length`; reply is a 4-byte header
  (`0x52 addrH addrL len`) + data.
- Codeplug XOR key: `b" EIY"`; positions whose key byte is `0x20` are not
  transformed (this is important — it prevents the low digit/name corruption
  that a naive XOR causes).

## Open work

- Full function map of the UI/radio state machine.
- Exact key code → label/action mapping by press test.
- Radio calibration tables in the `0xF000` EEPROM region.
- Build system / linker script for a replacement image.
