# KickEmulator

A desktop emulator for the Behringer Kick firmware (`0700-AAM`) and any fork of it. You give it a
firmware repository URL and a branch. It fetches that ref, builds it, and runs it against an
emulated front panel, so you can test a change to the Kick app, the audio engine or the Linux
image without a unit.

> **Status: under construction.** Nothing here runs a firmware yet. What works today is listed
> under [Status](#status). The plan is `/goal-standalone` on the firmware work branch
> `rbdone/goal-simulator-controls` (`.claude/commands/goal-standalone.md`).

The firmware itself needs no emulator code. The only coupling is the control-surface serial
protocol (`CPktMgr`, `ComProtocol/PktMgr`). The panel is a separate program that speaks the
microcontroller's side of that protocol over a transport: a pseudo-terminal, a TCP socket, or a
real serial port. The protocol headers and control ids (`ComProtocol/PktMgr`,
`Kick/modules/contracts/dcl`) come from the firmware ref under test, not from a copy, so a fork
that extends the protocol still works.

## Modes

Both modes use the same panel and the same checks.

| | Fast mode (`--mode fast`) | Faithful mode (`--mode vm`) |
|---|---|---|
| Runs | The fork's `Kick` app, built for the desktop (macOS, Linux) from the fork's source and connected to the panel over a pty | The fork's Linux arm64 `KT_Kick` build in a QEMU arm64 VM, with the panel on the VM's serial port |
| For | Quick edits and rebuilds, a debugger, scripted checks | The device's Linux code paths and services; CI on firmware pull requests |
| Needs in the firmware | It builds on the desktop, has a working serial port, and has a few generic, opt-in extension points (`KICK_SERIAL_DEVICE`, `KICK_TEST_CONTROL`, `KICK_AUDIO_PROBE`, ...) | A working serial port |
| **Cannot tell you** | Anything Linux-specific: systemd units, scripts, device nodes, Wi-Fi / Ethernet / CV / update screens (shown as "not available on this system") | Latency, CPU headroom on the A53 cores, the codec, the GPU driver, or the control-surface microcontroller's own behaviour |

Every result the emulator reports says which mode produced it and what that mode cannot tell you.

## Usage

```sh
./kickem fetch --firmware <url|path> --ref <branch|tag|full sha>   # works today
./kickem run --firmware https://github.com/KickEm-Tools/0700-AAM.git --ref main --mode fast
```

`fetch` clones or updates the ref, shallowly, into `$KICKEM_CACHE` (else `~/.cache/kickem`). It
prints the commit and the checkout path, which you can pass to CMake as `-DKICKEM_FIRMWARE_DIR`.
`run` parses its arguments, fetches, and takes the lock. It then says that building and running
come in phase 5 (`fast`) or phase 6 (`vm`), and exits with status 3.

Only one emulator instance runs per machine (one audio device, one panel). `run` holds a
machine-wide lock, `$KICKEM_LOCK`, by default `/tmp/kt_kickem.lock`. It is the same lock, in the
same format, as the in-tree simulator's `sim_launch.sh`, so the two never run at once. A lock
whose owner has died is reclaimed. Otherwise `run` waits up to `$KICKEM_LOCK_WAIT` seconds
(default 1800).

## Status

| Phase (`/goal-standalone`) | What | State |
|---|---|---|
| 4 | This repository: licence, layout, the firmware fetch, protocol targets from the fetched ref, `protocol_roundtrip`, the Starter Kit, `kickem fetch` | done |
| 5 | Fast mode: the panel speaks `CPktMgr` over a pty to the fork's desktop `Kick`; `kickem run --mode fast`; the checks ported to the wire | not started |
| 6 | Faithful mode: QEMU arm64 VM with the fork's `KT_Kick-linux-arm64`, panel on the VM's serial port | not started |
| 7 | Reusable CI workflow for forks (`test-firmware.yml`), panel release builds | not started |

## Layout

| Path | What |
|---|---|
| `panel/` | The front panel. Imported from the in-tree simulator; still links the firmware app until phase 5 (see `panel/README.md`) |
| `transport/` | pty, TCP and serial transports for the panel (interfaces only so far) |
| `cmake/` | `KickemFirmware.cmake`: fetches the firmware by URL and ref, and exposes the protocol targets |
| `tools/protocol_roundtrip/` | Encodes a BUTTON and a PAD packet with the fetched ref's `CPktMgr` and decodes them as the app's serial reader does, plus an LED packet the other way |
| `modes/fast/`, `modes/vm/` | The two modes (plans so far) |
| `checks/` | The in-tree simulator's check scripts, not ported yet (see `checks/README.md`) |
| `content/StarterKit/` | The Starter Kit: synthesised samples and a demo project, CC0 |
| `kickem` | The launcher (`fetch` works; `run` stops after the fetch and the lock until phase 5) |
| `.github/workflows/` | Empty until phase 7 |

## Building the protocol tool

```sh
# On macOS, as for the firmware build (it matters once JUCE is built, -DKICKEM_PROTOCOL_JUCE=juce):
export MACOSX_DEPLOYMENT_TARGET=12.0
cmake -S . -B build                                   # fetches KickEm-Tools/0700-AAM main
cmake -S . -B build -DKICKEM_FIRMWARE_URL=<url> -DKICKEM_FIRMWARE_REF=<ref>
cmake -S . -B build -DKICKEM_FIRMWARE_DIR=/path/to/your/0700-AAM   # a local checkout
cmake --build build && ./build/tools/protocol_roundtrip/protocol_roundtrip    # prints the bytes, PASS
```

See `cmake/README.md` for the options, including how the JUCE include in
`dcl_contracts.h` is satisfied.

## Content and branding

- **Starter Kit.** `content/StarterKit/` is synthesised from scratch by `make_starter_kit.py`
  (fixed seeds, Python standard library only) and is dedicated to the public domain under CC0 1.0, generator included.
  Its `LICENSE` sits beside it. Running the script reproduces the committed files byte for byte.
- **No logo.** The emulator draws no manufacturer logo or other trademarks: the area above the
  screen, where the unit carries its logo, is left blank.
- Never committed: firmware build outputs, VM disk images, Buildroot downloads, NXP firmware or
  driver blobs (see `.gitignore`).

## History

This repository started clean. The simulator was developed inside the firmware tree on
`rbdone/0700-AAM`, branch `rbdone/goal-simulator-controls`. The import commit names the source
commit (`6b6024ce`) and maps every imported file to its path there. That branch keeps the full
history and the audit record (`docs/sim_audit/`, `docs/DEVELOPMENT.md` sections 4 and 7).

## Licence

GNU Affero General Public License v3.0 or later (`AGPL-3.0-or-later`), the same as the firmware it
builds against. See `LICENSE`. The Starter Kit content is CC0 1.0 (`content/StarterKit/LICENSE`).
