# panel/: the emulated front panel

`kickem-panel` is the Kick's front panel drawn to scale, as its own program: 16 velocity pads,
the keys with their LED captions and SHIFT functions, P1-P4 and the jog wheel as endless
encoders, the slider and its LED ladder, and the MASTER / REC pots. It plays the control-surface
microcontroller's side of the serial protocol: it sends what the microcontroller sends, and it
shows the pad colours and key LEDs the app sends back. The Kick's screen is either embedded in the
display cut-out (`--screen embedded`, below) or the app's own window (`--screen window`, the
default when the panel is started by hand), in which case the cut-out shows the link's state.

It links JUCE's GUI modules and, from the firmware ref under test, only the protocol:
`ComProtocol/PktMgr` (`kickem::pktmgr`) and the control ids in
`Kick/modules/contracts/dcl/dcl_contracts.h` (`kickem::protocol`). No `kick_ui`, no `KickHome`, no
`SIM_BUILD`. It draws no logo.

```sh
kickem-panel [--transport pty|pty:<link>|tcp:<host>:<port>|serial:<dev>[@<baud>]]
             [--throttle [BAUD]] [--control FILE] [--quiet]
             [--screen window|embedded] [--screen-source unix:<path>|tcp:<host>:<port>]
```

`./kickem run` builds and starts it; to build it alone:

```sh
MACOSX_DEPLOYMENT_TARGET=12.0 cmake -S . -B build -DKICKEM_PROTOCOL_JUCE=juce \
      [-DKICKEM_FIRMWARE_DIR=<0700-AAM checkout>] [-DCPM_SOURCE_CACHE=<dir with JUCE>]
cmake --build build --target kickem-panel     # build/panel/kickem-panel, a plain executable
```

## Files

| File | What it holds |
|---|---|
| `Main.cpp` | The program: arguments, window, logger (stdout), wiring of panel, link and control channel |
| `PanelLink.{h,cpp}` | The microcontroller's side of the link: framing with the fetched `CPktMgr`, a reader and a writer thread, the handshake, `SimSerial:` logging |
| `kick_Simulator.{h,cpp}` | Panel geometry (canvas units measured from a photo of the unit), drawing, SHIFT semantics, pad-colour, LED and slider-ladder decoding, and the packet each control sends |
| `ScreenView.{h,cpp}` | The embedded screen: a small RFB client drawn in the display cut-out, and the mouse on it sent back as touches |
| `PanelControl.{h,cpp}` | The scripting channel (`--control FILE`): the panel half of the in-tree simulator's `SimControl` |
| `../transport/Transport.{h,cpp}` | pty, tcp and serial transports, and the throttle |

## On the wire

- **Frames.** Every packet is built with the fetched firmware's `CPktMgr::BuildAndTransmitPacket`
  and followed by one `'\n'`, because the app's reader takes `size + 7` bytes per frame. Each frame
  is written with one `write()`: the app's reader drops a frame that arrives in two reads, and the
  frame after it (firmware `docs/DEVELOPMENT.md` 8.4). A payload over 20 bytes is refused, since
  the app's receive buffer is 20 bytes with no bounds check.
- **The app's frames** have no trailing byte. The panel frames them itself (`'M' 'T'`, `size`),
  then runs each one through `CPktMgr::PushByteToRxFifo`, which checks the CRC. Each call starts
  at `'M'`, so the decoder's function-static partial-frame buffer is always cleared. An empty
  payload would never complete there, so those are checked with `CPktMgr::CrcGen` directly.
- **Handshake,** as `Kick/tests/fake_mcu.py` in the firmware does it. The app sends
  `EN_SYS_COMM_INIT [CONNECTED]` when it opens the port. The panel then sends "comm init"
  (`EN_SYS_CHECK_COMM_STATUS [1]`) every second until the app answers `EN_SYS_COMM_INIT [2]`,
  then "comm live" (`[2]`) every second until the app answers `EN_SYS_CHECK_COMM_STATUS [2]`
  (`Send Comm Live` in the app's log). Then it reports its version (`EN_SYS_GET_SW_VER [0, 1]`). A
  second connect frame restarts the handshake.
- **The pty.** The panel keeps the master side and also holds the slave side open, in raw mode,
  so the app's first bytes are not echoed and the master sees no hang-up when the app quits or
  restarts. A restarted app reconnects to the same path.
- **Throttle** (`--throttle`, default 230400 baud): whole frames are paced at 10 bits a byte in
  both directions, so a flood of LED or pad packets arrives as late as on the unit. Off by default.

Log lines, one per frame and direction (`--quiet` drops the per-frame lines):

```
SimSerial: listening on /dev/ttys000
SimSerial: rx SYSTEM cmd=2 data=1 | 4D 54 00 00 02 02 01 05
SimSerial: handshake: the app connected (EN_SYS_COMM_INIT [CONNECTED])
SimSerial: tx SYSTEM cmd=4 data=1 | 4D 54 00 00 04 02 01 A0 0A
SimSerial: rx SYSTEM cmd=2 data=2 | 4D 54 00 00 02 02 02 56
SimSerial: handshake: comm init answered after 1 try
SimSerial: tx SYSTEM cmd=4 data=2 | 4D 54 00 00 04 02 02 F3 0A
SimSerial: rx SYSTEM cmd=4 data=2 | 4D 54 00 00 04 02 02 F3
SimSerial: LIVE 0.455 s after the app connected (1 try)
```

With `KICK_SIM_LOG_SURFACE=1` the panel also logs every packet it acts on as
`SimSurface: <TYPE> cmd=<n> data=<...>` (the in-tree simulator's trace, which `checks/smoke`
reads).

## The embedded screen

With `--screen embedded --screen-source <socket>` the display cut-out holds `ScreenView`, an RFB
("VNC") client for the screen server at `<socket>`:

- **fast mode:** the firmware's opt-in screen mirror. `kickem run` starts `Kick` with
  `KICK_SCREEN_MIRROR=<socket>`: its main window then has no native window at all (a headless
  JUCE peer), renders with JUCE's software renderer, as the unit does, at most 30 times a second,
  and serves the screen there (firmware `docs/DEVELOPMENT.md` 9.5, `Kick/modules/test_control`);
- **faithful mode:** QEMU's own VNC server on the VM's display (`-vnc unix:<socket>`); no QEMU
  window is shown.

The view is part of the panel's canvas (730 x 428 canvas units for 1024 x 600 pixels), so it
scales with the window as everything else does. A click is a touch: mouse down, drags while the
button is held, mouse up become RFB pointer events at screen pixels (`ScreenView::toScreen`). In
fast mode the app hands them to its peer's `handleMouseEvent`, so JUCE dispatches them exactly as
it dispatches a native window's mouse or the unit's touch panel (list rows, drags, sliders,
popups); in faithful mode QEMU turns them into absolute events on the VM's usb-tablet, through
Weston and Xwayland. Nothing here uses the operating system's windows: it works with the Mac
locked and the panel covered, and the panel's control channel can click the screen too
(`screentouch`).

The connection is retried every 250 ms, so the panel may start first and the app or the VM may
restart. Only security type None is supported, with raw encoding and DesktopSize. Log lines:
`ScreenView: connected to ...`, `first frame <n> ms after connecting`, `touch down / up <x> <y>`,
`disconnected (...)`.

## SHIFT

SHIFT is a toggle on the panel (the unit's is momentary). It sends its press and release packets.
While it is on, a key reports its secondary function, the caption printed under it, as the unit's
microcontroller does: SHIFT + MAIN is reported as `EN_GRID`. Clicking a caption (GRID, PREFS, ...)
sends SHIFT pressed, that function, SHIFT released. The panel no longer writes the app's SHIFT
state: the app keeps its own, from the SHIFT packet. On a firmware without `fix/shift-state`
(KickEm-Tools/0700-AAM PR #16) the serial reader stores SHIFT where `isShiftPressed()` does not
look, so SHIFT + pad and the other SHIFT-held behaviours do not trigger, as on a unit.

## The control channel

`--control FILE`: the panel reads lines `<seq> <command> [args]` appended to FILE and logs
`PanelControl: ok <seq>` or `PanelControl: error <seq> <reason>`. `checks/lib/kickem_ctl` is the
client. Commands: `press NAME`, `down NAME [VEL]`, `up NAME`, `hold NAME SECS [VEL]`,
`turn P1..P4|JOG STEPS`, `slider 0..100`, `repeat MS N NAME`, `shot FILE.png [SCALE]`,
`record START DIR [FPS [SCALE]]` / `record STOP` (the panel rendered as `shot` does, FPS times a
second, into numbered PNGs and `frames.txt` with each frame's wall-clock time; 30 fps at about
11 ms a frame on an M4 Max, written on background threads; `docs/make_demo_video.sh` uses it),
`list [FILTER]`, `link`, `echo TEXT`, and for the embedded screen `screen` (state, frame rate,
touch-to-frame latency), `screentouch X Y [X2 Y2 [MS]]` (a click or a drag on it, through the
mouse's own path) and `screenshot FILE.png` (the last frame as received). Details in
`PanelControl.h`. It drives the panel's controls directly, so it
needs no window server and works while the Mac is locked.

Names: the printed captions (`MAIN`, `PUSH`, `TAP TEMPO`, `GRID`), `Pad 1` to `Pad 16`, the icon
keys `RECORD`, `OVERDUB`, `STOP`, `PLAY`, `PLAY START`, `BACK`, `COPY`, `MINUS`, `PLUS`, and
`key <id>` for any key by its `BtnLedSequence` id.

## History

Imported from the in-tree simulator (`Kick/apps/kickem/simulator/` on
`rbdone/goal-simulator-controls` at `6b6024ce`). Phase 5 of `/goal-standalone` made it its own
program: the in-memory `SimulatorInterfaceImpl` / `SimulatorHub` and the embedded `KickHome`
screen were removed, `SimControl` was split (its screen half is now the firmware's
`KICK_TEST_CONTROL` module), and `PanelLink` and `transport/` replaced the in-memory path.
