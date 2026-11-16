# Protocols — BF-F8HP Pro

Two independent serial protocols are exposed by the radio:

1. **KDH bootloader** (firmware update mode, SK1+SK2 at power-on)
2. **CPS protocol** (normal mode, used by CHIRP / the vendor CPS)

Both run at **115200 8N1** on the FTDI programming cable.

## 1. KDH bootloader

Packet format:

```
0xAA | cmd | arg | lenH | lenL | data... | crcH | crcL | 0xEF
```

CRC-16/CCITT (poly `0x1021`, init `0x0000`) over `cmd+arg+len+data`.
A successful reply has `arg = 0x06` (ACK). Error codes:
`0xE1` handshake, `0xE2` retryable verify, `0xE3` address,
`0xE4` flash write, `0xE5` command error.

Commands implemented by the official updater:

| Cmd | Name | Payload |
| --- | --- | --- |
| `0x01` | handshake | `"BOOTLOADER"` (10 bytes) |
| `0x04` | announce chunks | 1 byte: number of 1024-byte chunks |
| `0x03` | data | `arg` = chunk index, 1024-byte payload |
| `0x42` | enter bootloader | none (returns `0xE5` when already in boot mode) |
| `0x45` | end / commit | none |

**No read/backup command was found.** 34 other opcodes were tested
(`0x00,0x02,0x05…0xFF`) and all returned `0xE5`. Dumping the 4 KiB
bootloader requires SWD.

The `.kdhx` file is sent **as-is**; the bootloader descrambles it while
writing (see `docs/firmware.md`).

## 2. CPS / codeplug protocol

### Handshake

Send the ASCII magic `PROGRAMBF5RTECHU`, expect `0x06`.

Then send the follow-up magics and read the listed response lengths:

| Magic | Length | Response (factory radio) |
| --- | --- | --- |
| `0x46` | 16 | `01 36 01 74 04 00 05 20 02 00 02 60 00 03 50 03` |
| `0x4D` | 6 | `35 52 54 45 43 48` = `"5RTECH"` |
| `53 45 4E 44 12 0D 0A 0A 10 03 0D 02 11 0C 12 0A 11 06 04 0E 02 09 0D 00 00` | 1 | `0x06` |

### Frames

```
read:   'R' | addrHi | addrLo | len
write:  'W' | addrHi | addrLo | len | data...
reply:  0x52 | addrHi | addrLo | len | data...
```

The read command is **not bounds-checked**: any 16-bit address is accepted,
so the full 64 KiB space can be dumped. Write replies with a single `0x06`
ACK per block.

### Data scrambling

Codeplug blocks are XOR-scrambled with **`b" EIY"`**. For each position:

- key byte `k = key[i % 4]`
- if `k == 0x20`, do **not** transform
- otherwise skip if the byte is `0x00`, `0xFF`, `k`, or `k ^ 0xFF`
- else XOR with `k`

The `k == 0x20` rule matters: without it, channel names and the low
frequency digits get corrupted.

### Address map (factory V0.53)

| Range | Contents |
| --- | --- |
| `0x0000–0x803F` | 32-byte channel records (freqs, tones, flags, name) |
| `0x8000–0x807F` | settings (squelch, power, VOX, GPS, keys, …) |
| `0x8080–0x80BF` | VFO / extra |
| `0x9000–0x907F` | scan lists / extra |
| `0xA000–0xA2BF` | contact list (`ConTact 1…16`) |
| `0xB000–0xB…` | call/zone-channel entries with checksums |
| `0xC000` | blank (`0xFF`) |
| `0xD000–0xD09F` | zone names (`Home`, `GMRS`, `AIRBAND`, `SATCOM`, …) |
| `0xE000` | blank (`0xFF`) |
| `0xF000–0xFFFF` | calibration-ish tables |

Factory dump contains **217 programmed channels**: airband, VHF, 220 MHz,
UHF, GMRS.

## Backup and restore

`tools/backup.py`:

- reads all `0x0000–0xFFFF` blocks,
- does at least **two passes** and aborts unless they are byte-identical,
- saves `eeprom_raw_64k.bin`, `eeprom_decoded_64k.bin`, `backup.json` and a
  `MANIFEST.sha256`,
- optionally keeps the vendor firmware bundle alongside for application
  restore.

`tools/restore.py` writes the CHIRP map back (`0x0000/0x9000/0xA000/0xD000`)
and re-reads every block to verify. It is **dry-run by default**.

### What is not covered

- `0x08000000–0x08000FFF` KDH bootloader (no serial read command).
- Application flash (restore from the vendor `.kdhx`).
- Any flash read-protection state (do not attempt to disable it without a
  bootloader image).
