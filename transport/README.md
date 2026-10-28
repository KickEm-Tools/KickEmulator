# transport/

These carry the bytes between the panel and the firmware's control-surface serial port. The panel
plays the microcontroller's side of `CPktMgr`. It frames packets with the fetched firmware's
`PktMgr.cpp` (`kickem::pktmgr`) and moves them over one of these transports. Phase 4 has only the
interface, `Transport.h`; the implementations come in phases 5 and 6.

| Spec (`--transport`) | Used by | How it connects | Phase |
|---|---|---|---|
| `pty[:<symlink>]` | fast mode, macOS and Linux | The panel opens a pseudo-terminal (`posix_openpt`) and keeps the master side. The launcher passes the slave path to the app as `KICK_SERIAL_DEVICE=<path>` (the firmware's `feature/serialport`). The app opens it as it opens `/dev/ttymxc0` on the unit | 5 |
| `tcp:<host>:<port>` | faithful mode, and Windows | QEMU's guest UART on `-serial tcp:127.0.0.1:<port>,server,nowait`. The panel connects as the client. The guest's `KT_Kick` opens its real serial device and needs no variable | 6 |
| `serial:<device>[@<baud>]` | a real port | A USB-serial adapter, for example. Default 230400 8N1, as on the device | 5 |

Options shared by all of them:

- **Throttle** (`--baud 230400`): `write()` paces bytes as the device's UART would, so timing-sensitive
  code (key repeat, pad rolls, a flood of LED updates) sees realistic arrival times. Off by default
  in fast mode.
- **Logging**: every frame in each direction is logged as `SimSerial: tx|rx <hex>`, so a check can
  show both ends of the wire.

Wire format, as `tools/protocol_roundtrip` demonstrates: `'M' 'T' type dir cmd size data[size-1]
crc`, with CRC-8 (polynomial 0x31, initial value 0xFF). Frames from the panel carry one extra byte
after the CRC, because the app's reader takes `size + 7` bytes per frame (see `panel/README.md`).
