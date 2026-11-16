# Hardware notes — BF-F8HP Pro

All addresses are from the decrypted V0.53 `NRF` image unless stated otherwise.
The app is linked at flash base **`0x08001000`**.

## MCU

- **AT32F413 / AT32F415** family, Cortex-M4F.
- Evidence:
  - 92-entry vector table (16 system + **76 IRQs**, exactly the AT32F413/415
    IRQ count).
  - SP `0x200078e0` and an init table covering `0x20000000–0x200078e0`
    → **32 KiB SRAM**, fully used.
  - STM32F1-style peripheral map (AT32-compatible): GPIOA–F at
    `0x40010800…`, CRM at `0x40021000`, FLASH at `0x40022000`,
    DMA1/DMA2 at `0x40020000/0x40020400`, SPI1 at `0x40013000`,
    ADC1 at `0x40012400`.
- Part size: app image is 108 KiB; `0x08001000 + 0x1A7C4 = 0x0801B7C4`.
  A **128 KiB** part is the natural fit, but 256 KiB is not excluded.
- The exact package/part marking is still unconfirmed (needs teardown/SWD).

### Flash layout

| Range | Use |
| --- | --- |
| `0x08000000–0x08000FFF` | 4 KiB KDH bootloader (not in `.kdhx`) |
| `0x08001000–0x0801B7C3` | NRF application image |
| `0x08001000–0x0801B9AF` | NRFB application image |

### Used peripherals / IRQs

| Peripheral | Base | Notes |
| --- | --- | --- |
| GPIOA–F | `0x40010800…` | pin map below |
| SPI1 | `0x40013000` | external SPI NOR |
| ADC1 | `0x40012400` | PA0 / PA1 analog |
| USART1/2/3 | `0x40013800` / `0x40004400` / `0x40004800` | IRQ 37/38/39 enabled |
| UART4 | `0x40004C00` | IRQ 52 enabled |
| DMA1 ch5 | IRQ 15 | generic DMA completion |
| DMA1/DMA2 | `0x40020000` / `0x40020400` | |
| TIM3/4/5 | `0x40000400` / `0x40000800` / `0x40000C00` | |
| CRM / FLASH / PWR | `0x40021000` / `0x40022000` / `0x40007000` | |

## Pin map

### GPIOA

| Pin(s) | Mode | Function | Evidence |
| --- | --- | --- | --- |
| PA0, PA1 | analog | ADC inputs | pin init writes `CRL` analog mode |
| PA4 | output | **LCD backlight, active high** | backlight helper at `0x0800B20C` sets/clears bit 4; confirmed on hardware |
| PA5, PA7 | AF push-pull | SPI1 SCK / MOSI | pin init AF mode |
| PA6 | input floating | SPI1 MISO | pin init input mode |
| PA8 | output | SPI NOR CS | bit-banged/SPI1 code uses `GPIOA` bit 8 |
| PA9 | (not configured in main pin init) | USART1_TX (likely) | UV-5RH reference |
| **PA10** | input, pull-up | **PTT, active low** | `gpio_read(GPIOA, 0x400)` debounce path at `0x0800df28`; same as UV-5RH |
| PA11–PA14 | output | RF band / PA control (TBD) | pin init output mask |

### GPIOB

| Pin(s) | Mode | Function | Evidence |
| --- | --- | --- | --- |
| **PB0** | output | **FD6818 SCN** | `write_reg` at `0x0800fd40` clears/sets bit 0 |
| PB1 | input, pull-up | unknown separate key/detect (not matrix) | pin init; no read found yet |
| **PB2–PB5** | input, pull-up | **keypad columns** | key scan reads `(IDR >> 2) & 0xF` |
| **PB6** | output | **LCD CS** | LCD write helpers toggle bit 6 |
| **PB7** | output | **LCD RST** | LCD init toggles bit 7 |
| **PB8–PB12** | output | **LCD D0–D4** and **keypad rows** | LCD data writes; key scan drives these rows |
| **PB13–PB15** | output | **LCD D5–D7** | LCD data writes |

> **Important:** the keypad rows share PB8–PB12 with the LCD data bus.
> Scanning toggles these lines. This is safe only while the LCD `WR` strobe
> (`PF6`) is idle; open firmware must serialize display writes and key scans.

### GPIOC

| Pin(s) | Mode | Function | Evidence |
| --- | --- | --- | --- |
| **PC0** | input, pull-up | **power key** | debounce + power-down path at `0x080036e0` |
| PC1–PC4 | output | TBD | pin init output mask |
| **PC4** | output | **FD6818 SDA** | bit-bang byte routine at `0x0800f4ec` |
| **PC5** | output | **FD6818 SCK** | same routine |
| PC6 | output | TBD | pin init output mask |
| PC7 | input, pull-up | separate key/detect (PTT candidate is PA10) | pin init |
| PC8–PC11 | output | TBD | pin init output mask |
| **PC12** | output | **LCD RS / D/C** | LCD write helpers |
| PC13, PC15 | output | TBD (PC15 cleared during power-down) | pin init |
| **PC14** | output | **speaker amp enable, active high** | confirmed on hardware; 446 RX audio audible with PC14 high |

### GPIOD / GPIOF

| Pin(s) | Mode | Function |
| --- | --- | --- |
| PD2 | output | TBD |
| PF4, PF5 | output | TBD |
| **PF6** | output | **LCD WR** |
| PF7 | output | TBD |

## Keypad

Scan routine: **`0x08009bd8`**. Key table: **flash `0x08016ef0`**.

- Rows driven low one at a time: **PB12, PB8, PB9, PB10, PB11** (plus an idle
  phase with all high).
- Columns read from **GPIOB IDR bits 2–5**: **PB2, PB3, PB4, PB5**.
- Direct side keys when no row is driven: **PB2 = `0x01`, PB3 = `0x00`**.

### Key codes

| Row driven | PB2 | PB3 | PB4 | PB5 |
| --- | --- | --- | --- | --- |
| idle (all high) | `0x01` | `0x00` | — | — |
| PB12 low | `0x11` | `0x12` | `0x13` | `0x14` |
| PB8 low | `0x02` | `0x03` | `0x04` | — |
| PB9 low | `0x05` | `0x06` | `0x07` | `0x08` |
| PB10 low | `0x09` | `0x0a` | `0x0b` | `0x0c` |
| PB11 low | `0x0d` | `0x0e` | `0x0f` | `0x10` |

### Physical layout

Confirmed by the hello image's UART key test. The matrix shape (one 4-key
row, one 3-key row, three 4-key rows, two side keys) matches the usual
Baofeng UV-17Pro-style layout:

```
[Menu] [Up]   [Down] [Exit]      <- PB12
[1]    [2]    [3]    -
[4]    [5]    [6]    [V/M]       <- PB9
[7]    [8]    [9]    [A/B]       <- PB10
[*]    [0]    [#]    [Band]      <- PB11
```

Side keys (confirmed by press test):
**top = `PB2` = `0x01` (SK1)**, **bottom = `PB3` = `0x00` (SK2)**.

Confirmed by the hello image's UART key test:

| Key | Code | Key | Code |
| --- | --- | --- | --- |
| 1 | `0x02` | Menu | `0x11` |
| 2 | `0x03` | Up | `0x12` |
| 3 | `0x04`* | Down | `0x13` |
| 4 | `0x05` | Exit | `0x14` |
| 5 | `0x06` | V/M | `0x08` |
| 6 | `0x07` | A/B | `0x0c` |
| 7 | `0x09` | Band | `0x10` |
| 8 | `0x0a` | top side (SK1) | `0x01` |
| 9 | `0x0b` | bottom side (SK2) | `0x00` |
| * | `0x0d` | PTT | `PA10` low |
| 0 | `0x0e` | | |
| # | `0x0f` | | |

\* `3 = 0x04` follows directly from the key table/layout; the press test
observed the other keys in that row but not `3` itself.

## PTT

- **PA10, active low**, shared with USART1_RX (same design as the UV-5RH
  open firmware).
- Debounce: `0x0800df28` increments a counter while PA10 is low and acts
  after 3 consecutive low samples.
- `0x080140fc` waits for PA10 to return high (PTT release).
- `0x08013182` also reads PA10 before entering the TX state machine.
- The main UI loop also reads PA10 and, while it is low, checks for key code
  `0x0c`; that combo calls `0x08007448` (service/test path), separate from
  normal TX.
- Likely shared with **USART1_RX** (same trick as the UV-5RH open firmware:
  the pin is pulled up and the PTT switch pulls it low when the UART is idle).

## FD6818 / FD6818B

- `NRF` / `NRFB` map to **FD6818 / FD6818B** (chip IDs `0x4819` / `0x4829`
  on related radios).
- Bit-banged 3-wire bus:
  - **SCN = PB0**
  - **SCK = PC5**
  - **SDA = PC4** (bidirectional; input mode for reads)
- Bit order: **SCK low → set SDA → delay → SCK high → delay** (MSB first).
  This edge order is critical; the inverse order returns bogus register data.
- Write transaction: SCN low → 7-bit register address → 16-bit value (MSB
  first) → SCN high → SCK low (idle).
- Init function: **`0x0800e604`**; register writes go through
  **`0x0800fd40`**; bit-bang byte routine **`0x0800f4ec`**.
- State register `0x30`: RX `0xBFF1`, TX `0xC1FE`, tone `0x0302`,
  TX tone `0xC3FA`.
- Frequency registers `0x38` / `0x39`; bandwidth `0x43`
  (wide `0x3028`, narrow `0x4048`).
- **Confirmed on hardware:** chip ID **`0x4819`** (FD6818);
  RX at **446.000 MHz**; register readback matches writes
  (`0x30=0xBFF1`, `0x37=0x1F0F`, `0x38=0x8AC0`, `0x39=0x02A8`,
  `0x43=0x3028`); RSSI (`0x67`) baseline ~31–33 and jumps to **~111**
  when a 446.000 MHz signal is keyed.
- 446.000 MHz frequency word: `0x02A88AC0` = 44,600,000 (Hz / 10).
- PA11–PA14 are configured as outputs (band-select candidates, default low);
  RX worked with the default. A diagnostic sweep is available in the hello
  image (`b` command).
- **Audio path confirmed on hardware:** speaker amp enable is **PC14 active
  high**. RX audio registers used by the hello image:
  - `0x47 = 0x6140` (AF out, RX audio state 1)
  - `0x48 = 0x83FF` (max test volume; vendor AF path uses
    `0x800F | (cal << 4)`)
  - `0x78 = 0x0000` (squelch fully open for the test)
  - AF response from the vendor init: `0x74=0xFEF0`, `0x44=0x8D8F`,
    `0x45=0x3359`
  - `0x7D=0xE952` (mic sensitivity, from the vendor init)
- 446.000 MHz voice was clearly audible with PC14 high.
- **TX confirmed:** carrier, 1 kHz test tone, and normal **mic voice** work.
  The working V00 set at 145.100: `0x47=6140`, `0x48=BBBF`, `0x40=34EA`,
  `0x7D=E95F`, `0x7E=302E`, `0x19=1041`, `0x2A=4F18`, `0x2B=C000`,
  `0x28=0A40`, `0x50=3B20`, `0x52=028F`, `0x36=64D7`, `0x1A=1F80`,
  state `C1FE`, VHF front-end GPIO3=1/GPIO4=0. Earlier low mic audio came
  from omitting the full mic-path writes and using the UHF front-end.

## ST7735 LCD

- 8-bit 8080 parallel interface.
- **D0–D7 = PB8–PB15**, **CS = PB6**, **RST = PB7**, **RS/DC = PC12**,
  **WR = PF6**.
- Window function `0x08016890` writes `0x2A`/`0x2B`; data/command helpers at
  `0x080168c0` / `0x0801683c`.
- Init function **`0x080169f4`**: ST7735 sequence
  (`0x11, 0xB1, 0xB2, 0xB3, 0xB4, 0xC0…0xC5, 0xE0/0xE1, 0x3A, 0x36, 0x29`).
- Horizontal clamp in the driver is **160** pixels (`cmp r4,#0xA0`).
- Backlight: **PA4, active high** (`0x0800B20C`).
- **Confirmed on hardware:** the hello image initializes this panel and
  displays `446.000`, `ID 4819`, `RSSI`, and a live key code in the correct
  orientation.

## External SPI NOR

- Hardware **SPI1**, **CS = PA8** (PA5 SCK / PA6 MISO / PA7 MOSI).
- Read/write helpers around `0x080126f8` / `0x08012748` / `0x08012854`
  (`0x03` read, `0x02` page program, `0x20` sector erase).
- The CPS-visible 64 KiB address space is **not** this flash image; it is
  the separate config EEPROM space (see `docs/protocols.md`).

## Open questions

- Exact AT32 part number / package and confirmed flash size.
- `PB1`, `PC7`, PA11–PA14, PC1–PC11, PC13–PC15, PD2, PF4/5/7 functions.
- Which UART is the programming cable vs GPS vs other (USART1/2/3, UART4
  interrupts are all enabled). USART1 on PA9/PA10 is confirmed for the app's
  hello output.
