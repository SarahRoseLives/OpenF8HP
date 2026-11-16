#!/usr/bin/env python3
"""OpenF8HP BF-F8HP Pro UI mockup.

Small stdlib server + htmx.  The radio state lives here; the browser only
swaps HTML fragments.  No third-party Python dependencies.
"""

from __future__ import annotations

import argparse
import html
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent

POWERS = ("LOW", "MID", "HIGH")
BANDWIDTHS = ("WIDE", "NARROW")
CTCSS = ("OFF", "67.0", "88.5", "100.0", "123.0")
STEPS = (("5.0", 5000), ("12.5", 12500), ("25.0", 25000))

MEMORIES = (
    {"name": "CALL", "freq": 146_520_000, "tone": "OFF"},
    {"name": "SIMPLEX", "freq": 446_000_000, "tone": "OFF"},
    {"name": "RPT", "freq": 147_000_000, "tone": "88.5"},
    {"name": "VHF", "freq": 145_100_000, "tone": "OFF"},
    {"name": "UHF 2", "freq": 446_500_000, "tone": "OFF"},
    {"name": "GMRS", "freq": 462_550_000, "tone": "67.0"},
)

MENU = (
    ("SQUELCH", "squelch"),
    ("POWER", "power"),
    ("CTCSS", "ctcss"),
    ("BANDWIDTH", "bandwidth"),
    ("STEP", "step"),
    ("BACKLIGHT", "backlight"),
    ("ABOUT", "about"),
)

THEMES = ("authentic", "hybrid", "modern")
THEME_LABELS = {
    "authentic": "AUTHENTIC",
    "hybrid": "HYBRID",
    "modern": "MODERN",
}


def fmt_hz(hz: int) -> str:
    return f"{hz / 1_000_000:.3f}"


def clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


class Radio:
    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.reset()

    # ---------------------------------------------------------------- state
    def reset(self) -> None:
        theme = getattr(self, "theme", "hybrid")
        self.screen = "boot"
        self.return_screen = "vfo"
        self.vfo = [446_000_000, 145_100_000]
        self.active = 0
        self.mem_idx = 1
        self.menu_idx = 0
        self.entry = ""
        self.volume = 7
        self.squelch = 3
        self.power = 2
        self.ctcss = 0
        self.bandwidth = 0
        self.step_idx = 1
        self.backlight = 4
        self.battery = 86
        self.rssi = 44
        self.monitor = False
        self.locked = False
        self.theme = theme
        self.tx = False

    def frequency(self) -> int:
        if self.screen == "memory" or (
            self.screen == "tx" and self.return_screen == "memory"
        ):
            return MEMORIES[self.mem_idx]["freq"]
        return self.vfo[self.active]

    def channel_label(self) -> str:
        hz = self.frequency()
        for mem in MEMORIES:
            if mem["freq"] == hz:
                return mem["name"]
        return f"VFO {'A' if self.active == 0 else 'B'}"

    def tone(self) -> str:
        return CTCSS[self.ctcss]

    def mode(self) -> str:
        return "FM" if self.bandwidth == 0 else "NFM"

    def power_short(self) -> str:
        return POWERS[self.power][0]

    def step_label(self) -> str:
        return STEPS[self.step_idx][0]

    def step_hz(self) -> int:
        return STEPS[self.step_idx][1]

    def setting_value(self, key: str) -> str:
        if key == "squelch":
            return str(self.squelch)
        if key == "power":
            return POWERS[self.power]
        if key == "ctcss":
            return CTCSS[self.ctcss]
        if key == "bandwidth":
            return BANDWIDTHS[self.bandwidth]
        if key == "step":
            return STEPS[self.step_idx][0]
        if key == "backlight":
            return str(self.backlight)
        return ""

    def adjust_setting(self, key: str, direction: int) -> None:
        if key == "squelch":
            self.squelch = clamp(self.squelch + direction, 0, 9)
        elif key == "power":
            self.power = clamp(self.power + direction, 0, 2)
        elif key == "ctcss":
            self.ctcss = (self.ctcss + direction) % len(CTCSS)
        elif key == "bandwidth":
            self.bandwidth = (self.bandwidth + direction) % len(BANDWIDTHS)
        elif key == "step":
            self.step_idx = (self.step_idx + direction) % len(STEPS)
        elif key == "backlight":
            self.backlight = clamp(self.backlight + direction, 1, 5)

    def state(self) -> dict[str, object]:
        return {
            "screen": self.screen,
            "vfo": [fmt_hz(v) for v in self.vfo],
            "active_vfo": "A" if self.active == 0 else "B",
            "frequency": fmt_hz(self.frequency()),
            "channel": self.channel_label(),
            "memory": self.mem_idx + 1,
            "rssi": self.rssi,
            "battery": self.battery,
            "volume": self.volume,
            "squelch": self.squelch,
            "power": POWERS[self.power],
            "ctcss": self.tone(),
            "bandwidth": BANDWIDTHS[self.bandwidth],
            "step": self.step_label(),
            "monitor": self.monitor,
            "locked": self.locked,
            "tx": self.tx,
            "entry": self.entry,
            "theme": self.theme,
        }

    # ---------------------------------------------------------------- actions
    def key(self, key: str) -> None:
        if self.locked and key not in ("sk2",):
            return
        if self.screen == "boot":
            return
        if key == "sk2":
            self.locked = not self.locked
            return
        if self.screen == "tx":
            return
        if key == "sk1":
            self.monitor = not self.monitor
            return
        if key in "0123456789":
            self._digit(key)
            return
        if key == "star":
            self.entry = ""
            return
        if key == "hash":
            self._commit_entry()
            return
        if key == "up":
            self._up()
            return
        if key == "down":
            self._down()
            return
        if key == "menu":
            self._menu()
            return
        if key == "exit":
            self._exit()
            return
        if key == "vm":
            self._vm()
            return
        if key == "ab":
            self._ab()
            return
        if key == "band":
            self._band()
            return

    def _digit(self, digit: str) -> None:
        if self.screen != "vfo":
            return
        if len(self.entry) >= 6:
            self.entry = ""
        self.entry += digit
        if len(self.entry) == 6:
            hz = int(self.entry) * 1000
            if 100_000_000 <= hz <= 600_000_000:
                self.vfo[self.active] = hz
            self.entry = ""

    def _commit_entry(self) -> None:
        if self.screen == "vfo" and len(self.entry) == 6:
            hz = int(self.entry) * 1000
            if 100_000_000 <= hz <= 600_000_000:
                self.vfo[self.active] = hz
            self.entry = ""

    def _up(self) -> None:
        if self.screen == "vfo":
            if self.entry:
                return
            self.vfo[self.active] = clamp(
                self.vfo[self.active] + self.step_hz(), 100_000_000, 600_000_000
            )
        elif self.screen == "memory":
            self.mem_idx = (self.mem_idx + 1) % len(MEMORIES)
        elif self.screen == "menu":
            self.menu_idx = clamp(self.menu_idx + 1, 0, len(MENU) - 1)
        elif self.screen == "settings":
            self.adjust_setting(MENU[self.menu_idx][1], 1)

    def _down(self) -> None:
        if self.screen == "vfo":
            if self.entry:
                return
            self.vfo[self.active] = clamp(
                self.vfo[self.active] - self.step_hz(), 100_000_000, 600_000_000
            )
        elif self.screen == "memory":
            self.mem_idx = (self.mem_idx - 1) % len(MEMORIES)
        elif self.screen == "menu":
            self.menu_idx = clamp(self.menu_idx - 1, 0, len(MENU) - 1)
        elif self.screen == "settings":
            self.adjust_setting(MENU[self.menu_idx][1], -1)

    def _menu(self) -> None:
        if self.screen == "vfo":
            self.screen = "menu"
            self.menu_idx = 0
        elif self.screen == "memory":
            self.vfo[self.active] = MEMORIES[self.mem_idx]["freq"]
            self.screen = "vfo"
        elif self.screen == "menu":
            key = MENU[self.menu_idx][1]
            self.screen = "about" if key == "about" else "settings"
        elif self.screen == "settings":
            self.screen = "menu"
        elif self.screen == "about":
            self.screen = "menu"

    def _exit(self) -> None:
        if self.screen == "vfo":
            self.entry = ""
        elif self.screen == "memory":
            self.screen = "vfo"
        elif self.screen == "menu":
            self.screen = "vfo"
        elif self.screen == "settings":
            self.screen = "menu"
        elif self.screen == "about":
            self.screen = "menu"

    def _vm(self) -> None:
        if self.screen == "vfo":
            self.screen = "memory"
        elif self.screen == "memory":
            self.screen = "vfo"
        else:
            self.screen = "vfo"

    def _ab(self) -> None:
        if self.screen not in ("vfo", "memory", "tx"):
            self.screen = "vfo"
        self.active = 1 - self.active
        self.entry = ""

    def _band(self) -> None:
        if self.screen not in ("vfo", "memory"):
            self.screen = "vfo"
        self.vfo[self.active] = 446_000_000 if self.vfo[self.active] < 300_000_000 else 145_100_000
        self.entry = ""

    def ptt(self, on: bool) -> None:
        if on and not self.tx:
            self.return_screen = "vfo" if self.screen == "boot" else self.screen
            self.tx = True
            self.screen = "tx"
        elif not on and self.tx:
            self.tx = False
            self.screen = self.return_screen

    def sim(self, form: dict[str, str]) -> None:
        if "rssi" in form:
            self.rssi = clamp(int(form["rssi"]), 0, 100)
        if "battery" in form:
            self.battery = clamp(int(form["battery"]), 0, 100)
        if "volume" in form:
            self.volume = clamp(int(form["volume"]), 0, 9)

    def set_theme(self, theme: str) -> None:
        self.theme = theme if theme in THEMES else "hybrid"

    def boot_end(self) -> None:
        if self.screen == "boot":
            self.screen = "vfo"

    # ---------------------------------------------------------------- render
    def signal_bars(self) -> int:
        if not self.monitor and self.rssi < self.squelch * 10:
            return 0
        return clamp((self.rssi + 19) // 20, 1, 5) if self.rssi else 0

    def s_meter(self) -> str:
        if self.signal_bars() == 0:
            return "S0"
        return f"S{clamp(self.rssi // 12, 1, 9)}"

    def meter_html(self) -> str:
        level = self.signal_bars() * 2
        bars = "".join(
            f'<i class="{"on" if i < level else ""}"></i>' for i in range(10)
        )
        return (
            '<div class="meter"><span>S</span>'
            f'<div class="meter-bars">{bars}</div>'
            f'<span>{self.s_meter()}</span></div>'
        )

    def status_html(self, tx: bool = False) -> str:
        bars = self.signal_bars()
        sig = "".join(
            f'<i class="{"on" if i < bars else ""}"></i>' for i in range(5)
        )
        flags = []
        if self.monitor:
            flags.append("MON")
        if self.locked:
            flags.append("LCK")
        if tx:
            flags = ["TX"]
        low = " low" if self.battery < 20 else ""
        tone = "T" if self.ctcss else ""
        return (
            '<div class="status">'
            f'<span class="vfo-label">{"A" if self.active == 0 else "B"}</span>'
            f'<span class="batt{low}"><i style="width:{self.battery}%"></i></span>'
            f'<span class="pct">{self.battery}</span>'
            f'<span class="sig">{sig}</span>'
            f'<span class="mode">{self.mode()}</span>'
            f'<span class="pwr">{self.power_short()}</span>'
            f'<span class="tone">{tone}</span>'
            f'<span class="flags">{" ".join(flags)}</span>'
            "</div>"
        )

    def soft_html(self) -> str:
        labels = {
            "vfo": ("MENU", "MEM", "MON", "LOCK"),
            "memory": ("TUNE", "EXIT"),
            "menu": ("SELECT", "BACK"),
            "settings": ("SAVE", "BACK"),
            "about": ("BACK",),
            "tx": ("PTT",),
        }.get(self.screen, ("MENU", "EXIT"))
        return '<div class="soft">' + "".join(
            f"<span>{label}</span>" for label in labels
        ) + "</div>"

    def entry_display(self) -> str:
        e = self.entry
        if len(e) <= 3:
            return e
        return f"{e[:3]}.{e[3:]}"

    def render_lcd(self, oob: bool = True) -> str:
        screen = self.screen
        if screen == "boot":
            body = (
                '<div class="lcd-screen boot">'
                '<div class="boot-name">AD8NT</div>'
                '<div class="boot-sub">OPENF8HP</div>'
                '<div class="boot-line">MOCKUP v0.1</div>'
                '<div class="boot-timer" hx-post="/boot/end" '
                'hx-trigger="load delay:900ms" hx-target="#lcd" '
                'hx-swap="innerHTML"></div>'
                "</div>"
            )
        elif screen == "tx":
            body = (
                '<div class="lcd-screen tx-screen">'
                f'{self.status_html(tx=True)}'
                f'<div class="freq tx-freq">{fmt_hz(self.frequency())}</div>'
                f'<div class="sub">TRANSMIT · {POWERS[self.power]} · {self.mode()}</div>'
                '<div class="tx-body"><span>MIC</span><div class="wave">'
                '<i></i><i></i><i></i><i></i><i></i><i></i><i></i>'
                "</div></div>"
                f'{self.soft_html()}'
                "</div>"
            )
        elif screen == "memory":
            start = clamp(self.mem_idx - 1, 0, max(0, len(MEMORIES) - 4))
            rows = []
            for i in range(start, min(start + 4, len(MEMORIES))):
                mem = MEMORIES[i]
                sel = " sel" if i == self.mem_idx else ""
                rows.append(
                    f'<div class="row{sel}"><span>{">" if i == self.mem_idx else " "} '
                    f'{i + 1:02d} {mem["name"]}</span>'
                    f'<span>{fmt_hz(mem["freq"])}</span></div>'
                )
            body = (
                '<div class="lcd-screen memory-screen">'
                f'{self.status_html()}'
                f'<div class="freq small">{fmt_hz(self.frequency())}</div>'
                f'<div class="sub">MEM {self.mem_idx + 1:02d} · '
                f'{MEMORIES[self.mem_idx]["name"]} · {MEMORIES[self.mem_idx]["tone"]}</div>'
                f'<div class="list mem-list">{"".join(rows)}</div>'
                f'{self.soft_html()}'
                "</div>"
            )
        elif screen == "menu":
            start = clamp(self.menu_idx - 1, 0, max(0, len(MENU) - 5))
            rows = []
            for i in range(start, min(start + 5, len(MENU))):
                name, key = MENU[i]
                sel = " sel" if i == self.menu_idx else ""
                rows.append(
                    f'<div class="row{sel}"><span>{">" if i == self.menu_idx else " "} '
                    f'{name}</span><span>{self.setting_value(key)}</span></div>'
                )
            body = (
                '<div class="lcd-screen menu-screen">'
                f'{self.status_html()}'
                '<div class="title">MENU</div>'
                f'<div class="list menu-list">{"".join(rows)}</div>'
                f'{self.soft_html()}'
                "</div>"
            )
        elif screen == "settings":
            name, key = MENU[self.menu_idx]
            value = self.setting_value(key)
            body = (
                '<div class="lcd-screen settings-screen">'
                f'{self.status_html()}'
                f'<div class="title">{name}</div>'
                f'<div class="value-large">{value}</div>'
                '<div class="hint">UP / DOWN to change</div>'
                f'{self.soft_html()}'
                "</div>"
            )
        elif screen == "about":
            body = (
                '<div class="lcd-screen about-screen">'
                f'{self.status_html()}'
                '<div class="title">OPENF8HP</div>'
                '<div class="about-text">BTECH BF-F8HP PRO<br>'
                'UI MOCKUP v0.1<br><br>AD8NT<br>'
                f'{fmt_hz(self.vfo[0])} / {fmt_hz(self.vfo[1])}<br>'
                'HTMX + PYTHON</div>'
                f'{self.soft_html()}'
                "</div>"
            )
        else:
            freq = self.entry_display() if self.entry else fmt_hz(self.frequency())
            freq_cls = "freq entry" if self.entry else "freq"
            body = (
                '<div class="lcd-screen vfo-screen">'
                f'{self.status_html()}'
                f'<div class="{freq_cls}">{freq}</div>'
                f'<div class="sub">{self.channel_label()} · {self.tone()} · {self.mode()}</div>'
                '<div class="vfo-body">'
                f'<div class="mini-row"><span>SQL {self.squelch}</span>'
                f'<span>STEP {self.step_label()}</span>'
                f'<span>VOL {self.volume}</span></div>'
                f'<div class="mini-row"><span>{self.s_meter()}</span>'
                f'<span>{self.mode()}</span>'
                f'<span>{POWERS[self.power]}</span></div>'
                f'{self.meter_html()}'
                "</div>"
                f'{self.soft_html()}'
                "</div>"
            )

        if not oob:
            return body
        state = esc(json.dumps(self.state(), indent=2))
        return body + f'<pre id="state" hx-swap-oob="innerHTML">{state}</pre>'

    def render_app(self) -> str:
        theme_class = f"theme-{self.theme}"
        keypad = [
            ("1", "1", "num"),
            ("2", "2", "num"),
            ("3", "3", "num"),
            ("vm", "V/M", "fn"),
            ("4", "4", "num"),
            ("5", "5", "num"),
            ("6", "6", "num"),
            ("ab", "A/B", "fn"),
            ("7", "7", "num"),
            ("8", "8", "num"),
            ("9", "9", "num"),
            ("band", "BAND", "fn"),
            ("star", "*", "num"),
            ("0", "0", "num"),
            ("hash", "#", "num"),
            ("menu", "MENU", "fn"),
        ]
        key_buttons = "".join(
            '<button type="button" class="key %s" data-key="%s" '
            'hx-post="/key" hx-vals=\'{"key":"%s"}\' hx-target="#lcd" '
            'hx-swap="innerHTML">%s</button>' % (cls, key, key, label)
            for key, label, cls in keypad
        )
        nav_buttons = "".join(
            '<button type="button" class="key nav-key" data-key="%s" '
            'hx-post="/key" hx-vals=\'{"key":"%s"}\' hx-target="#lcd" '
            'hx-swap="innerHTML">%s</button>' % (key, key, label)
            for key, label in (("up", "UP"), ("down", "DOWN"), ("exit", "EXIT"))
        )
        side_buttons = "".join(
            '<button type="button" class="key side-key" data-key="%s" '
            'hx-post="/key" hx-vals=\'{"key":"%s"}\' hx-target="#lcd" '
            'hx-swap="innerHTML">%s</button>' % (key, key, label)
            for key, label in (("sk1", "SK1"), ("sk2", "SK2"))
        )
        theme_buttons = "".join(
            '<button type="button" class="theme-btn%s" hx-post="/theme" '
            'hx-vals=\'{"theme":"%s"}\' hx-target="#app" hx-swap="outerHTML">%s</button>'
            % (" active" if self.theme == name else "", name, THEME_LABELS[name])
            for name in THEMES
        )
        state = esc(json.dumps(self.state(), indent=2))
        lcd = self.render_lcd(oob=False)

        return f'''<div id="app" class="{theme_class}" data-theme="{self.theme}">
  <header class="page-head">
    <div class="eyebrow">OPENF8HP · INTERFACE MOCKUP</div>
    <div class="head-status"><span class="dot"></span> VOICE ONLY · htmx</div>
  </header>

  <main class="stage">
    <section class="radio" aria-label="BTECH BF-F8HP Pro mockup">
      <div class="radio-top">
        <span class="led" aria-hidden="true"></span>
        <span class="brand">BTECH <b>BF-F8HP PRO</b></span>
        <span class="mock-tag">MOCKUP</span>
      </div>

      <div class="screen-bezel">
        <div class="lcd-scale">
          <div id="lcd" aria-live="polite">{lcd}</div>
        </div>
      </div>

      <div class="front">
        <div class="keypad">{key_buttons}</div>
        <div class="nav">{nav_buttons}</div>
        <div class="side">
          <button type="button" class="ptt" data-ptt title="Push to talk">PTT</button>
          {side_buttons}
          <div class="knob" title="Volume">VOL</div>
        </div>
      </div>

      <div class="radio-foot"><span>AD8NT</span><span>146.520 · 446.000</span></div>
    </section>

    <aside class="sim">
      <div class="sim-head"><h2>SIMULATOR</h2><span class="pill">VOICE</span></div>
      <div class="theme-row">{theme_buttons}</div>

      <label class="slider">
        <span>SIGNAL <output>{self.rssi}</output></span>
        <input type="range" min="0" max="100" step="1" name="rssi" value="{self.rssi}"
          hx-post="/sim" hx-trigger="input changed delay:120ms"
          hx-include="closest .sim" hx-target="#lcd" hx-swap="innerHTML">
      </label>
      <label class="slider">
        <span>BATTERY <output>{self.battery}</output></span>
        <input type="range" min="0" max="100" step="1" name="battery" value="{self.battery}"
          hx-post="/sim" hx-trigger="input changed delay:120ms"
          hx-include="closest .sim" hx-target="#lcd" hx-swap="innerHTML">
      </label>
      <label class="slider">
        <span>VOLUME <output>{self.volume}</output></span>
        <input type="range" min="0" max="9" step="1" name="volume" value="{self.volume}"
          hx-post="/sim" hx-trigger="input changed delay:120ms"
          hx-include="closest .sim" hx-target="#lcd" hx-swap="innerHTML">
      </label>

      <div class="sim-actions">
        <button type="button" class="sim-ptt" data-ptt>HOLD PTT</button>
        <button type="button" class="ghost" hx-post="/reset" hx-target="#app"
          hx-swap="outerHTML">RESET</button>
      </div>

      <details class="state-box">
        <summary>STATE</summary>
        <pre id="state">{state}</pre>
      </details>

      <div class="help">
        <b>KEYS</b> 1–9/0 entry · ↑/↓ tune · Enter menu · Esc exit<br>
        V = V/M · A = A/B · B = band · S = monitor · L = lock · Space = PTT
      </div>
    </aside>
  </main>

  <div id="poll" hx-get="/lcd" hx-trigger="every 1s" hx-target="#lcd"
    hx-swap="innerHTML"></div>
</div>'''


RADIO = Radio()


def static_file(path: str) -> tuple[bytes, str] | None:
    rel = path.lstrip("/")
    if not rel:
        rel = "index.html"
    target = (ROOT / rel).resolve()
    if target != ROOT and ROOT not in target.parents:
        return None
    if not target.is_file():
        return None
    mime = {
        ".html": "text/html; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".js": "text/javascript; charset=utf-8",
        ".ttf": "font/ttf",
        ".woff2": "font/woff2",
        ".svg": "image/svg+xml",
        ".png": "image/png",
        ".ico": "image/x-icon",
        ".json": "application/json; charset=utf-8",
        ".md": "text/markdown; charset=utf-8",
    }.get(target.suffix.lower(), "application/octet-stream")
    return target.read_bytes(), mime


class Handler(BaseHTTPRequestHandler):
    server_version = "OpenF8HP-Mockup/0.1"

    def _send(self, body: bytes, content_type: str, status: int = 200) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _html(self, body: str, status: int = 200) -> None:
        self._send(body.encode("utf-8"), "text/html; charset=utf-8", status)

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path
        if path in ("/", "/index.html"):
            asset = static_file("index.html")
            if asset is None:
                self._html("index.html missing", 404)
            else:
                self._send(asset[0], asset[1])
            return
        if path == "/app":
            with RADIO.lock:
                self._html(RADIO.render_app())
            return
        if path == "/lcd":
            with RADIO.lock:
                self._html(RADIO.render_lcd())
            return
        if path == "/state":
            with RADIO.lock:
                self._send(
                    json.dumps(RADIO.state(), indent=2).encode(),
                    "application/json; charset=utf-8",
                )
            return
        if path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return
        asset = static_file(path)
        if asset is None:
            self._html("not found", 404)
        else:
            self._send(asset[0], asset[1])

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        if length > 1_000_000:
            self._html("request too large", 413)
            return
        raw = self.rfile.read(length).decode("utf-8", "replace")
        form = {k: v[-1] for k, v in parse_qs(raw, keep_blank_values=True).items()}
        path = parsed.path
        with RADIO.lock:
            if path == "/key":
                RADIO.key(form.get("key", ""))
                self._html(RADIO.render_lcd())
            elif path == "/ptt":
                RADIO.ptt(form.get("state", "off") == "on")
                self._html(RADIO.render_lcd())
            elif path == "/sim":
                try:
                    RADIO.sim(form)
                except (TypeError, ValueError):
                    pass
                self._html(RADIO.render_lcd())
            elif path == "/theme":
                RADIO.set_theme(form.get("theme", "hybrid"))
                self._html(RADIO.render_app())
            elif path == "/reset":
                RADIO.reset()
                self._html(RADIO.render_app())
            elif path == "/boot/end":
                RADIO.boot_end()
                self._html(RADIO.render_lcd())
            else:
                self._html("not found", 404)

    def log_message(self, fmt: str, *args: object) -> None:
        # Keep the mockup terminal quiet unless something is wrong.
        if args and str(args[1]).startswith("4"):
            super().log_message(fmt, *args)


def main() -> None:
    parser = argparse.ArgumentParser(description="OpenF8HP BF-F8HP Pro UI mockup")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.daemon_threads = True
    print(f"OpenF8HP mockup: http://{args.host}:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
