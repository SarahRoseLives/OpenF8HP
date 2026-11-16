# hello — BF-F8HP Pro bring-up image

Bare-metal image that runs from `0x08001000` after the KDH bootloader jumps
to it. It brings up the UART, the ST7735 parallel LCD, the keypad matrix and
PTT — no RF, audio or codeplug.

Confirmed working on a BF-F8HP Pro (NRF, V0.53).

What it does:

- sets HSI to 8 MHz,
- USART1 `PA9` TX @ **115200**,
- resets/inits the **ST7735** on its 8-bit 8080 bus and turns the backlight
  on (`PA4` high),
- draws `HELLO` and updates a `KEY XX` line with the last key code,
- scans keypad rows `PB8–PB12` / columns `PB2–PB5`,
- reads PTT (`PA10`, active low) and shows a red square while pressed,
- prints key/PTT events over the cable.

## LCD wiring

| Signal | Pin |
| --- | --- |
| D0–D7 | `PB8–PB15` |
| CS | `PB6` |
| RST | `PB7` |
| RS/DC | `PC12` |
| WR | `PF6` |
| Backlight | `PA4` (active high) |

The keypad rows share `PB8–PB12` with LCD D0–D4. CS/RST/WR are held inactive
while scanning so the LCD cannot latch keypad noise.

## Build

Requires `clang`, `ld.lld`, `llvm-objcopy` and Python 3.

```sh
make
```

Outputs `hello.bin` (padded to 8192 bytes) and `hello.kdhx` (KDH-scrambled,
8 chunks).

## Flash

Put the radio into update mode (power off, hold **SK1+SK2**, power on), then:

```sh
python3 ../../tools/flash_kdhx.py hello.kdhx --write --yes --read
```

`--read` reopens the port after the bootloader reboots and prints whatever the
app sends.

## Expected result

UART:

```
OPENF8HP hello
USART1 PA9 @115200, LCD ST7735, keypad PB8-12/PB2-5, PTT PA10
.
key=0x0e 0
key=release
```

LCD:

- dark blue background,
- white `HELLO`,
- yellow `KEY XX` that follows the last key press,
- red square while PTT is held,
- backlight on.

## Confirmed key codes

`1=0x02 2=0x03 3=0x04 4=0x05 5=0x06 6=0x07 7=0x09 8=0x0a 9=0x0b`,
`*=0x0d 0=0x0e #=0x0f`, `Menu=0x11 Up=0x12 Down=0x13 Exit=0x14`,
`V/M=0x08 A/B=0x0c Band=0x10`, top side `0x01`, bottom side `0x00`,
PTT `PA10` low.

## Recovery

The KDH bootloader is **not** overwritten. If the app does not behave:

1. power off,
2. hold SK1+SK2 and power on,
3. flash the vendor V0.53 `.kdhx` from
   `work/backups/<timestamp>/vendor/F8HPPRO_V53_Update_Bundle/` with
   `tools/flash_kdhx.py` (or the FlintWave flasher).

Keep the verified EEPROM backup from `tools/backup.py` before experimenting.
