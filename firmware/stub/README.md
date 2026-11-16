# RAM flash stub

Small stub linked at `0x20004000`, embedded in the app as `stub_blob.c`, and
copied to RAM by the app's `x` command. It runs entirely from RAM while it
erases/programs the app region at `0x08001000`, then resets.

Protocol (115200 8N1):

- stub -> `RFSTUB\n`
- host -> `FLSH` + `u32le length` + `u32le crc32`
- stub erases the 1 KB sectors covering the image, then -> `OK\n`
- host -> `u16le chunk length` + bytes (<=256)
- stub -> `0x06` per chunk
- stub -> `DONE\n`, resets

The KDH bootloader and update mode are untouched; use them if a live flash
fails. `PAGE_SIZE` is **1 KB** on this AT32F413.
