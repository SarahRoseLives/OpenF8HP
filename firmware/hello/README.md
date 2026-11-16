# hello — BF-F8HP Pro bring-up image

Bare-metal image that runs from `0x08001000` after the KDH bootloader jumps
to it. It brings up the UART, ST7735 LCD, keypad, PTT and **FD6818 RX**.

Confirmed working on a BF-F8HP Pro (NRF, V0.53):

- FD6818 chip ID **0x4819**
- RX at **446.000 MHz**
- RSSI baseline ~31–33, jumps to **~111** when a 446.000 MHz signal is keyed
- **audible 446.000 FM voice**; speaker amp enable **PC14 active high**
- LCD **Command layout**: blue rail, IDLE/TX, `FM ANALOG`,
  `VFO A / 446.000`, large `SIMPLEX`, SQL/CTCSS/PWR, `READY / RSSI`
- keypad matrix mapped, PTT, backlight
- TX carrier + 1 kHz tone + **normal mic voice** on 145.100 and 446.000 (V00)

## Command UI

The 446 build uses a P25-inspired service-radio layout adapted to 160×128
and analog FM. The left rail is blue in RX and **red in TX**; the bottom soft
bar has been removed.

- `IDLE` / `TX` state box, `FM ANALOG`
- `VFO A / 446.000 CH --`
- large channel name: `SIMPLEX`
- `SQL 3 CTCSS OFF PWR H`
- `READY` / `RSSI -xxx` footer
- `CONTROL SQUELCH STEP POWER` with values
- 1× font for status/footer, 2× for the channel name
- blue rail: `0xC3DF`; red TX rail: `0xF800`

P25 trunking, talkgroups, zones, and control channels are not implemented;
the labels are analog equivalents. The keypad soft labels are visual for now;
menu/memory functions are not wired to this screen yet.

## Battery and PA

- Battery is read on ADC channel 1; raw 62 currently maps to about 63%.
  The `48..70` raw range is the initial 0..100% calibration; adjust after a
  bench-supply sweep.
- The top icon and the two-digit percentage are live; low battery turns red.
- `SK1` cycles PA **LOW → MID → HIGH**. Targets are `0x30D7`, `0x48D7`,
  `0x64D7`; the default is LOW.
- TX has a 60 s timeout; it drops the carrier and returns to RX until PTT is
  released.
- `v` over serial prints `bat raw=… pct=… pa=…`.

## Screen capture

- 4bpp shadow framebuffer; `lcd_fill_rect`/`lcd_pixel` mirror every draw.
- `s` over serial sends `A5 5A`, width/height, bpp 4, then RLE pairs
  `count,value`, terminated by `00 00`.
- `python3 ../../tools/screencap.py --port /dev/ttyUSB0 --out screen.bmp`
  writes a 160×128 BMP; `--stream --fps 2 --out frames/` repeats.

## What it does

- HSI 8 MHz, USART1 `PA9` TX @ 115200
- ST7735 8-bit 8080 LCD init, backlight `PA4`
- FD6818 3-wire init and `446.000 MHz` RX
- AF out `0x47=0x6140`, volume `0x48=0x83FF` (max test value),
  squelch `0x78=0` (open)
- speaker amp **PC14 high**
- read RSSI (`0x67`) and update the signal bars / RSSI footer
- keypad matrix mapped (`scan_once` present; not wired to Command yet)
- PTT `PA10` active low; TX redraws with a red rail
- UART commands: `d` = register dump, `b` = PA11–PA14 band sweep,
  `r` = read RSSI, `s` = screen capture, `v` = battery/PA

## Wiring

| Block | Pins |
| --- | --- |
| LCD | D0–D7 `PB8–PB15`, CS `PB6`, RST `PB7`, RS `PC12`, WR `PF6`, BL `PA4` |
| FD6818 | SCN `PB0`, SCK `PC5`, SDA `PC4` |
| Keypad | rows `PB8–PB12`, columns `PB2–PB5` |
| PTT | `PA10` (active low, shared with USART1_RX) |
| Speaker amp | `PC14` (active high) |

## FD6818 clocking (important)

The vendor bit-bang order is:

```
SCK low -> set SDA -> delay -> SCK high -> delay
```

The first version used `SCK high -> SDA -> SCK low`; the chip still answered
some reads, but returned the wrong chip ID / register values. Correcting the
edge order made the ID read `0x4819` and RSSI live.

Register readback after init:

```
reg00=4819   chip id (FD6818)
reg30=bff1   RX on
reg37=1f0f   RX power
reg38=8ac0   freq word low
reg39=02a8   freq word high  -> 0x02A88AC0 = 446.000 MHz / 10 Hz
reg43=3028   wide bandwidth
reg67=xxxx   RSSI, (val & 0x1FF) >> 1
```

## Build

Requires `clang`, `ld.lld`, `llvm-objcopy` and Python 3.

```sh
make
```

Outputs `hello.bin` (padded to 32768 bytes) and `hello.kdhx` (KDH-scrambled,
32 chunks).

## Flash

Put the radio into update mode (power off, hold **SK1+SK2**, power on), then:

```sh
python3 ../../tools/flash_kdhx.py hello.kdhx --write --yes --read
```

## UART diagnostics

While the app is running:

```sh
python3 -c 'import serial,time; s=serial.Serial("/dev/ttyUSB0",115200,timeout=.2); time.sleep(1); s.write(b"d"); time.sleep(1); print(s.read(512).decode("ascii","replace"))'
```

`d` dumps the FD6818 registers, `b` sweeps the PA11–PA14 band-select
candidates and prints RSSI for each, `r` reads RSSI once.

## Audio test values

The hello image uses **max volume** (`0x48=0x83FF`) and **squelch open**
(`0x78=0`). A production firmware should read the volume knob / codeplug
settings instead. 446.000 MHz FM voice was clearly audible with PC14 high
and the programming cable unplugged (the cable disconnects the speaker).

## Key codes

`1=0x02 2=0x03 3=0x04 4=0x05 5=0x06 6=0x07 7=0x09 8=0x0a 9=0x0b`,
`*=0x0d 0=0x0e #=0x0f`, `Menu=0x11 Up=0x12 Down=0x13 Exit=0x14`,
`V/M=0x08 A/B=0x0c Band=0x10`, top side `0x01` (SK1), bottom side `0x00`
(SK2), PTT `PA10` low.

## Recovery

The KDH bootloader is **not** overwritten. If the app does not behave:

1. power off,
2. hold SK1+SK2 and power on,
3. flash the vendor V0.53 `.kdhx` from
   `work/backups/<timestamp>/vendor/F8HPPRO_V53_Update_Bundle/` with
   `tools/flash_kdhx.py` (or the FlintWave flasher).

Keep the verified EEPROM backup from `tools/backup.py` before experimenting.
