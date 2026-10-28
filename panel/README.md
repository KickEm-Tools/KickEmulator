# panel/: the emulated front panel

This is the Kick's front panel drawn to scale: 16 velocity pads, the keys with their LED captions
and SHIFT functions, P1-P4 and the jog wheel as endless encoders, the slider and its LED ladder,
and the MASTER / REC pots. It sends what the control-surface microcontroller sends, and it shows
the pad colours and key LEDs the app sends back.

## What is here now (phase 4)

The files were imported unchanged from the in-tree simulator (`Kick/apps/kickem/simulator/` on
`rbdone/goal-simulator-controls` at `6b6024ce`). There is one edit:

| File | What it holds |
|---|---|
| `kick_Simulator.{h,cpp}` | Panel geometry (canvas units measured from a photo of the unit), drawing, SHIFT semantics (secondary ids), pad-colour and LED decoding, and the packets each control sends |
| `kick_SimControl.{h,cpp}` | The scripting channel (`KICK_SIM_CONTROL=<file>`, driven by `checks/sim_ctl.sh`). It has a panel half and a screen half, see below |
| `kick_SimulatorInterfaceImpl.{h,cpp}` | The in-memory seam: `SimulatorHub` passes `CPktMgr::HWMsg_t` between the panel and the app's `input_events` through `Factory<SimulatorInterface>` (`SIM_BUILD`) |

The edit: the panel draws no logo. On the work branch it printed the theme asset `Sim Behringer
Logo.png` above the screen, or the word "behringer" without it; both are trademarks, so that area
is left blank and there is no option to add one. The change was compiled with
`-fsyntax-only` against the firmware work branch's `KT_KickEM` flags (`compile_commands.json`).

**This directory does not build on its own yet**, and the top-level CMake does not build it.
It compiles only inside the firmware tree, as part of the `KT_KickEM` app on the work branch.

## What still links the firmware app

These are the dependencies phase 5 removes. Each one is listed with what it becomes.

| Dependency | Where | Phase 5 |
|---|---|---|
| `#include <kick_ui/kick_ui.h>`: the whole app UI | `kick_Simulator.h` | Removed. The panel includes only the protocol headers from the fetched ref (`kickem::protocol`: `PktMgr/PktMgr.hpp`, `contracts/dcl/dcl_contracts.h`) plus JUCE GUI modules |
| `kick_ui::KickHome::Instance()`, the app's 1024x600 screen embedded as a child component and scaled into the bezel | `Simulator()`, `~Simulator()`, `resized()` | Removed. The app runs in its own process and window. The bezel cut-out stays empty, or later mirrors the app's screen through the test-control `shot` command |
| `ui::simScreen()` (a `SIM_BUILD` seam in `kick_ControlBase.h`) | `Simulator()`, `~Simulator()` | Removed. The firmware parents overlays to `ui::overlayRoot()` itself (`feature/desktop-seams`) |
| `contracts::shiftBtnState.store()`, which writes the app's SHIFT global directly | `setShiftHeld()` | Removed. Only the SHIFT packet is sent. **Finding (from reading the code, not yet observed):** the firmware's serial path stores SHIFT in `Controls::shiftBtnState` (`input_events.cpp:248`), not in the `contracts::shiftBtnState` that `DclContractsHelper::isShiftPressed()` reads (`kick_table.cpp`, `kick_padButtonGroup.cpp`). Over the wire those SHIFT behaviours may then be dead, on the unit too. Check this in phase 5 and file it as a FIX if confirmed |
| `SimulatorHub::sendToApp()` / `fromApp`, `SimulatorInterfaceImpl`, `REGISER_FACTORY( SimulatorInterface, ... )`, `input_events/kick_SimulatorInterface.h`, `common/common.h`, `sigslot` | `kick_SimulatorInterfaceImpl.*`, every `send*` in `kick_Simulator.cpp` | Replaced by a `PanelLink`. It frames each `HWMsg_t` with `CPktMgr::BuildAndTransmitPacket` from the fetched ref and writes it to a `transport/` (pty, TCP, serial). It reads the app's frames back with `CPktMgr::PushByteToRxFifo` and turns them into `handleFromApp()`. The in-memory files are deleted |
| Screen half of `SimControl`: `click`, `tap`, `clickat`, `list`, `windows`, `status`, the screen part of `shot`, `ui::simFocusTarget()`, `statusProvider` (set by the app's `Main.cpp` from the engine) | `kick_SimControl.cpp` | Moves to the firmware as the generic, opt-in `KICK_TEST_CONTROL` module (`Kick/modules/test_control`, decision 1 in `docs/STANDALONE_SPLIT.md`). Faithful mode uses QEMU input injection instead |
| Panel half of `SimControl`: `press`, `down`, `up`, `hold`, `turn`, `slider`, `repeat`, `echo`, the panel part of `shot` | `kick_SimControl.cpp` | Stays here as the panel's own control channel. It drives the panel's controls, which send packets over the wire |
| The app's `Main.cpp`, which runs the app and the panel in one process with one message loop | not imported (work branch `Kick/apps/kickem/Main.cpp`) | The panel gets its own `main`: window, `--transport pty\|tcp:<host:port>\|serial:<dev>`, the control channel, and the machine-wide lock |

Protocol details phase 5 must match. These were read from the firmware's
`Kick/modules/input_events/input_events.cpp` and `ComProtocol/PktMgr/PktMgr.cpp`, and
`tools/protocol_roundtrip` shows them:

- A frame is `'M' 'T' type dir cmd size data[size-1] crc`. `size` counts the data plus the CRC
  byte. The CRC is CRC-8, polynomial 0x31, initial value 0xFF, over every byte before it.
- The app's reader expects **one trailing byte after the CRC** (`packetLength = size + 7`). It
  keeps that byte off the decoder. Without it, frames are lost. A frame at the end of a read is
  too short to be taken. A frame that follows another loses its `'M'`, because the reader skips
  that byte as the previous frame's trailer. The panel must send the trailing byte, presumably
  the `'\n'` that is commented out in `BuildAndTransmitPacket`.
- The app's writer sends frames without that trailing byte.
- `PushByteToRxFifo` keeps its partial frame in a function-`static` vector, shared by every
  `CPktMgr` in the process. The panel needs one decoder per process, or its own framing on top.
- A frame with `size == 1` (no data) is never completed by `PushByteToRxFifo`: the CRC byte is
  taken as data. Every packet the panel sends carries data, so this does not bite yet.
