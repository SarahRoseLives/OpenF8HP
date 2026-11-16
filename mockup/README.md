# BF-F8HP Pro UI mockup

A standalone, interactive web mockup for the BF-F8HP Pro. It shows a
scale model of the physical radio — 160×128 LCD, keypad, side keys, PTT,
volume knob — beside a simulator panel. The UI is a real state machine driven
by **htmx** and a tiny Python server.

This is a design/prototype tool. It does **not** flash firmware, talk to the
radio, or implement APRS/KISS. The mockup is voice-only; TNC screens can be
added later.

## Run

```sh
python3 mockup/serve.py
# open http://127.0.0.1:8080/
```

Options:

```sh
python3 mockup/serve.py --host 0.0.0.0 --port 8090
```

No Flask, npm, or runtime network. htmx and the pixel font are vendored under
`mockup/vendor/`. Python 3.10+.

## What works

- Boot splash → VFO A/B
- Frequency entry (six digits, e.g. `446000` → 446.000)
- Direct tuning with Up/Down and the current step
- VFO A/B, band switch, memory list, tune from memory
- PTT press-and-hold: TX screen, then back to the previous screen
- Squelch, power, CTCSS, bandwidth, step, backlight settings
- Monitor (SK1) and keypad lock (SK2)
- RSSI, battery, volume sliders in the simulator
- About screen
- Three switchable skins: **Authentic LCD**, **Hybrid tactical**, **Modern**

## Themes

The theme buttons re-render the shell and LCD from server state:

| Theme | Intent |
| --- | --- |
| Authentic | Black/amber pixel LCD, square keys; closest to the real panel |
| Hybrid | Black/amber/cyan, larger contrast, red TX; default |
| Modern | Dark blue/teal, rounded shell and cards; still 160×128 |

All three use the same state machine and the same screen content.

## Keys

Click the virtual keys, or use the keyboard:

| Key | Action |
| --- | --- |
| `1`–`9`, `0` | Frequency entry in VFO |
| `*` / `#` | Clear entry / commit six digits |
| `↑` / `↓` | Tune, select memory/menu, change setting |
| `Enter` | Menu / select / save |
| `Esc` | Exit / back |
| `V` | VFO / memory |
| `A` | VFO A/B |
| `B` | Band switch |
| `S` | Monitor (SK1) |
| `L` | Lock (SK2) |
| `Space` | PTT (press and hold) |

The LCD bottom labels are soft-key hints; the mockup uses the physical
keypad rather than making the LCD itself touchable.

## Screens

- **Boot** — AD8NT / OpenF8HP splash
- **VFO** — frequency, channel, S-meter, battery, mode, power, CTCSS
- **Memory** — six mock channels; Up/Down; Menu tunes
- **TX** — red TX state, power, mode, animated mic
- **Menu** — Squelch, Power, CTCSS, Bandwidth, Step, Backlight, About
- **Settings** — Up/Down edits, Menu saves, Exit backs out
- **About** — callsign and mockup version

## Architecture

```text
mockup/
  index.html          htmx bootstrap; loads /app into #app
  serve.py            stdlib HTTP server, Radio state, HTML renderers
  css/radio.css       radio shell, LCD, keypad, simulator
  css/themes.css      authentic / hybrid / modern variables
  js/input.js         keyboard, PTT pointer events, htmx.ajax helpers
  vendor/htmx.min.js  htmx 2.0.4
  vendor/fonts/       Silkscreen 400/700
```

htmx endpoints:

| Endpoint | Purpose |
| --- | --- |
| `GET /app` | Full shell + LCD + simulator |
| `GET /lcd` | LCD fragment + OOB state |
| `GET /state` | JSON state inspector |
| `POST /key` | Keypad/nav key |
| `POST /ptt` | `state=on` / `state=off` |
| `POST /sim` | RSSI, battery, volume |
| `POST /theme` | Re-render shell with a theme |
| `POST /reset` | Reset to boot splash |
| `POST /boot/end` | Leave the boot splash |

State is in memory and resets when the server restarts. The LCD polls
`/lcd` every second so S-meter/battery changes from the simulator are picked
up; htmx also swaps an out-of-band `#state` block after each action.

## Design constraints

- On-device LCD is **160×128**, keypad-only, no touch.
- The mockup uses a 5×7-style pixel font; the real firmware has a 5×7 font
  and 1×/2× glyph scaling.
- The radio UI should stay readable at 160×128; the simulator panel is not
  part of the device UI.
- The web page is a design surface; the actual firmware drawing primitives
  will be C, not htmx.

## Notes

- Mock channels are illustrative (`CALL`, `SIMPLEX`, `RPT`, `VHF`, `UHF 2`,
  `GMRS`).
- No audio is played; the TX mic animation is cosmetic.
- APRS/KISS is intentionally out of scope for this mockup.
- The existing TNC experiment remains in `work/hello_tnc.c` (git-ignored).

## Licenses

- htmx 2.0.4 — MIT; see `vendor/README.md`.
- Silkscreen — SIL Open Font License 1.1; see `vendor/README.md`.
