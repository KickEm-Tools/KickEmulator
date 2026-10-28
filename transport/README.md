# transport/

These carry the bytes between the panel and the firmware's control-surface serial port. The panel
plays the microcontroller's side of `CPktMgr`. It frames packets with the fetched firmware's
`PktMgr.cpp` (`kickem::pktmgr`) and moves them over one of these transports: `Transport.h`, with
all three implementations in `Transport.cpp` (POSIX: macOS and Linux). `kickem-panel --transport
<spec>` picks one.

| Spec (`--transport`) | Used by | How it connects | Phase |
|---|---|---|---|
| `pty[:<symlink>]` | fast mode, macOS and Linux | The panel opens a pseudo-terminal (`posix_openpt`), keeps the master side, and also holds the slave side open in raw mode (no echo of the app's first bytes; no hang-up when the app quits, so it can reconnect). It logs `SimSerial: listening on <slave path>`. The launcher passes that path to the app as `KICK_SERIAL_DEVICE` (the firmware's `feature/serialport`), which opens it as it opens `/dev/ttymxc0` on the unit. With `:<symlink>`, that path is made a link to the slave | implemented and used (phase 5) |
| `tcp:<host>:<port>` | faithful mode, and Windows | QEMU's guest UART on `-serial tcp:127.0.0.1:<port>,server,nowait`. The panel connects as the client, retrying for 30 s. The guest's `KT_Kick` opens its real serial device and needs no variable | implemented; checked against a TCP server playing the app's first frame (the panel answered comm init); QEMU in phase 6 |
| `serial:<device>[@<baud>]` | a real port | A USB-serial adapter, for example. 8N1, raw, no flow control; default 230400, as on the device | implemented; checked on a pty slave opened as a serial device at 230400 (the panel answered comm init); no USB adapter tried |

Options shared by all of them:

- **Throttle** (`kickem-panel --throttle [BAUD]`, `kickem run --throttle`; default 230400): frames
  are paced as the device's UART would carry them, 10 bits a byte, so timing-sensitive code (key
  repeat, pad rolls, a flood of LED updates) sees realistic arrival times. Whole frames are paced,
  not single bytes: the app's reader loses a frame that arrives in two reads. The panel also
  delays the app's frames by their wire time. Off by default in fast mode.
- **Logging**: every frame in each direction is logged as `SimSerial: tx|rx <hex>`, so a check can
  show both ends of the wire.

Wire format, as `tools/protocol_roundtrip` demonstrates: `'M' 'T' type dir cmd size data[size-1]
crc`, with CRC-8 (polynomial 0x31, initial value 0xFF). Frames from the panel carry one extra byte
after the CRC, because the app's reader takes `size + 7` bytes per frame (see `panel/README.md`).
