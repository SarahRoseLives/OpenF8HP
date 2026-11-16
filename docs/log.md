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
- **LCD confirmed on hardware:** white `HELLO` on black, upright,
  `KEY XX` updates on press, backlight on, PTT red square works.
- Extended hello to **FD6818 RX at 446.000 MHz** with an RSSI display and
  UART diagnostics (`d` register dump, `b` PA11–PA14 band sweep, `r` RSSI).
- Found and fixed the bit-bang edge order: **SCK low → SDA → SCK high**.
  The inverse order returned chip ID `0x34D0` and static RSSI 65.
- After the fix: chip ID **`0x4819`** (FD6818), RX state `0xBFF1`,
  frequency word `0x02A88AC0`, RSSI baseline ~31–33, jumped to **111**
  while a 446.000 MHz signal was keyed. **RF RX confirmed.**
- **Audio path confirmed:** FD6818 AF out `0x47=0x6140`, volume
  `0x48=0x83FF` (max test), squelch `0x78=0` open; **speaker amp enable
  PC14 active high**. 446.000 MHz voice is clearly audible.
- **TX carrier confirmed:** UHF PA path, 446.000 MHz, state `0xC1FE`;
  1 kHz tone via `0x70/0x71` + `0xC3FA` is received (max deviation
  `0x40=0x00FF`, PA target 100/low gain `0x36=0x64D7`).
- **Mic audio not yet:** with `0x47` muted → carrier only; with `0x47`
  unmuted → thump/buzz. Swept PF4/5/7, PD2, PC1/2/3/6/8/9/10/11/13/15 and
  PA11–14 as candidate mic-bias outputs; no voice. The mic input may be on
  the MCU ADC (PA0/PA1) or need a codec/register init not yet found.
- LCD background changed to **black**.
- **ADC mic check:** PA0/PA1 read ~`0062` at rest and did not change with
  speech. A passive sweep of PF4/5/7, PD2, PC1/2/3/6/8/9/10/11/13/15 and
  all-high also produced no ADC response. The mic is not on PA0/PA1 (or its
  bias is not a simple GPIO). Mic TX therefore needs the vendor codec/audio
  init or a mic-bias register that has not been found yet.
- **Mic-gain experiments:** voice is present on every TX variant but very
  low. Tried `0x7D` (0xE95F..0xFFFF), `0x40` dev/dev_en, `0x7E` TX-MIC
  DC-filter/AGC-fix, `0x19` MIC-AGC disable, `0x50` TX unmute, `0x52` AF
  filter, `0x2A`/`0x2C` (0x7FFF/0x5705), PA target/gain, and PB1/PC7
  pull-ups. None increased the level. PA0/PA1 ADC also shows no mic
  response. Remaining likely causes: a hardware mic preamp/bias line or a
  codec/1-wire init not yet identified.
- **Mic resolved (V00, 145.100):** after adding the full mic-path register
  writes (`0x2A=4F18`, `0x2B=C000`, `0x28=0A40`, `0x50=3B20`, `0x52=028F`,
  `0x7E=302E`, `0x19=1041`) and the VHF TX front-end, TX voice works
  normally. Earlier `rf_tx` omitted those writes and used the UHF front-end.
  Locked V00: `0x47=6140`, `0x48=BBBF`, `0x40=34EA`, `0x7D=E95F`,
  `0x36=64D7`, `0x1A=1F80`, state `C1FE`.
- **446 voice verified:** the earlier 446 mic-low test used the default
  `0x40=0x00FF`; the 446 voice main now calls `set_variant(0)`
  (`0x40=0x34EA`), and V00 mic + RX/RSSI/keypad work at 446.000.
- **Command UI (446.000):** ported the P25-inspired service-radio layout to
  the real 160×128 LCD: blue left rail, `IDLE`/`TX` box, `FM ANALOG`,
  `VFO A / 446.000 CH --`, large `SIMPLEX`, `SQL/CTCSS/PWR`,
  `READY`/`RSSI`, and the footer. The bottom soft bar was removed; the rail
  turns red in TX. P25 trunking is not supported; labels are FM/analog
  equivalents. The image is now 32 KiB / 32 chunks.
- The radio currently runs the **446 Command UI** image. The 145.100 mic
  image is preserved as `work/hello_mic_v00.c`; the 446 voice image as
  `work/hello_446_voice.c`; the TNC source/build is in `work/hello_tnc.c` /
  `work/hello_tnc.kdhx`.
- **Battery/PA:** ADC channel 1 reads raw 62 → 62/63%; the top icon and
  percentage are live. `SK1` cycles PA LOW/MID/HIGH (`0x30D7`/`0x48D7`/
  `0x64D7`), default LOW; 60 s TX timeout. The `48..70` raw range is the
  initial calibration.
- **Screen capture:** 4bpp shadow framebuffer + RLE over UART (`s`), decoded
  by `tools/screencap.py` to BMP; `--stream` repeats. The actual 160×128 UI
  was captured over the programming cable.
- **Live flashing:** resident 4 KB RAM stub at `0x20004000`, copied by `x`
  and linked in the app as `stub_blob.c`. The stub erases/programs the app
  region from RAM (AT32F413 sector size is **1 KB**), then resets.
  `tools/flash_live.py` sends the raw app; KDH update mode remains recovery.
  Verified end-to-end over the programming cable.
- **Splash:** 160×128 4bpp wordmark-only `OPENF8HP` boot screen (no subtitle,
  model, or `BOOTING...`). Live stub updates show the same splash with only a
  progress bar.

### Next

- Flash the hello image and capture the UART/keypad output.
- Confirm key labels/order with a press test (or PCB inspection).
- Trace remaining unassigned pins (PB1, PC7, PA4/11–14, PC1–11/13–15,
  PD2, PF4/5/7).
- Identify the exact AT32 part and whether flash RDP is enabled (SWD would
  answer both).
