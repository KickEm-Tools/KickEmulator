# KickEmulator

A desktop emulator for the Behringer Kick firmware (`0700-AAM`) and any fork of it. You give it a
firmware repository URL and a branch. It fetches that ref, builds it, and runs it against an
emulated front panel, so you can test a change to the Kick app, the audio engine or the Linux
image without a unit.

> **Status:** fast mode runs on macOS (Apple Silicon) and the smoke test passes over the wire.
> The other checks, faithful mode (a VM) and CI come next; see [Status](#status). The plan is
> `/goal-standalone` on the firmware work branch `rbdone/goal-simulator-controls`
> (`.claude/commands/goal-standalone.md`).

The firmware itself needs no emulator code. The only coupling is the control-surface serial
protocol (`CPktMgr`, `ComProtocol/PktMgr`). The panel is a separate program that speaks the
microcontroller's side of that protocol over a transport: a pseudo-terminal, a TCP socket, or a
real serial port. The protocol headers and control ids (`ComProtocol/PktMgr`,
`Kick/modules/contracts/dcl`) come from the firmware ref under test, not from a copy, so a fork
that extends the protocol still works.

## Quick start (macOS)

Needs Xcode's command-line tools, CMake 3.22+, git and Python 3.8+.

```sh
git clone https://github.com/KickEm-Tools/KickEmulator.git && cd KickEmulator
./kickem run --ref integration/standalone        # fetch, build, start the panel and Kick, connect them
```

Two windows open: the panel, and the Kick's screen (the fork's own `Kick` app). On the Projects
screen, press **PUSH** on the panel to load the Starter Kit, then **PLAY** (the round green key).
Ctrl-C in the terminal stops both. The first run downloads JUCE (about 170 MB) into
`~/.cache/kickem/cpm` unless `CPM_SOURCE_CACHE` points at a cache that has it.

`integration/standalone` is a scratch branch of `KickEm-Tools/0700-AAM` that merges every open
pull request. `main` cannot run yet, because the changes fast mode needs are still in review (see
[Pointing it at your fork](#pointing-it-at-your-fork)).

**How long it takes** (Apple Silicon, 16 cores, JUCE already in the CPM cache), measured by
`checks/smoke` from a cold cache on 2026-10-28:

| Step | Time |
|---|---|
| fetch (shallow clone) | 8 s |
| first build: `Kick` 114 s, panel 14 s | 128 s |
| from the end of the build to the handshake | 2.4 s |
| from the end of the build to sound out of the Starter Kit | 7.3 s, including the smoke test's 2 s pause after loading (target: 30 s) |

## Modes

Both modes use the same panel and the same checks.

| | Fast mode (`--mode fast`) | Faithful mode (`--mode vm`) |
|---|---|---|
| Runs | The fork's `Kick` app, built for the desktop (macOS, Linux) from the fork's source and connected to the panel over a pty | The fork's Linux arm64 `KT_Kick` build in a QEMU arm64 VM, with the panel on the VM's serial port |
| For | Quick edits and rebuilds, a debugger, scripted checks | The device's Linux code paths and services; CI on firmware pull requests |
| Needs in the firmware | It builds on the desktop, has a working serial port, and has a few generic, opt-in extension points (`KICK_SERIAL_DEVICE`, `KICK_TEST_CONTROL`, `KICK_AUDIO_PROBE`, ...) | A working serial port |
| State | works on macOS; Linux desktop not tried | phase 6, not started |

## What each mode cannot tell you

- **Fast mode** runs the desktop build, not the device's Linux build. It cannot tell you anything
  Linux-specific: systemd units, scripts, device nodes, and the Wi-Fi / Ethernet / CV /
  firmware-update screens, which show as "not available on this system". Timing, latency and CPU
  load are your computer's, not the unit's A53 cores. The panel is an emulation of the
  microcontroller's protocol, not its firmware.
- **Faithful mode** cannot tell you latency, CPU headroom on the A53 cores, the codec, the GPU
  driver, or the control-surface microcontroller's own behaviour.

`kickem run` and every check print this at the start.

## Usage

```sh
./kickem fetch [--firmware URL|PATH] [--ref REF]     # clone or update into ~/.cache/kickem/firmware
./kickem build [--firmware URL|PATH] [--ref REF]     # fetch, build Kick and the panel, install the Starter Kit
./kickem run   [--firmware URL|PATH] [--ref REF] [--mode fast] [--detach] [--fresh] [--probe OUT.wav]
               [--throttle [BAUD]] [--no-build] [--out DIR]
./kickem stop                                        # stop a --detach run, release the lock
./kickem status
```

- `--firmware` defaults to `https://github.com/KickEm-Tools/0700-AAM.git`; a local repository path
  works too. `--ref` is a branch, a tag or a full commit SHA (default `main`).
- `--fresh` restores the build's `demos/` and `templates/` from the checkout and drops `prefs/`,
  so saves and recent projects from an earlier run do not leak in.
- `--probe OUT.wav` sets `KICK_AUDIO_PROBE`: the app writes its master output there and logs
  `AudioProbe: t=<n>s peak=<x>dBFS rms=<y>dBFS` each second.
- `--throttle` paces the panel's link at the unit's 230400 baud.
- `KICK_AUDIO_INPUT` defaults to `silence` (no path from the microphone to the speakers); set it to
  `device` for the real input.
- Environment: `KICKEM_CACHE` (default `~/.cache/kickem`), `CPM_SOURCE_CACHE` (default
  `<cache>/cpm`), `KICKEM_LOCK` (default `/tmp/kt_kickem.lock`), `KICKEM_LOCK_WAIT` (default 1800 s).

Logs of a run are in `~/.cache/kickem/run` (or `--out`): `kick.log` (the app: `InputEvents:`,
`Send Comm Live`, `Project: loaded`, `AudioProbe:`, `TestControl:`) and `panel.log` (the panel:
`SimSerial:` for every frame in both directions, with hex; `SimSurface:` with
`KICK_SIM_LOG_SURFACE=1`; `PanelControl:`). `checks/lib/kickem_ctl` drives a running emulator from
scripts: panel keys over the wire, the screen through the firmware's test-control file
(`checks/README.md`).

**One instance per machine.** The app is single-instance and the emulator assumes one audio
device and one panel. `run` holds a machine-wide lock, `/tmp/kt_kickem.lock`: a directory with the
owner's pid (the running app's, once started), in the same format as the in-tree simulator's
`sim_launch.sh`, so the two never run at once. It also waits while any `Kick` or `KT_KickEM`
process runs without the lock. A lock whose owner has died is reclaimed; otherwise `run` waits.

## Pointing it at your fork

```sh
./kickem run --firmware https://github.com/<you>/0700-AAM.git --ref <your-branch>
./kickem run --firmware ~/src/0700-AAM --ref <your-branch>     # a local clone: commit first, it fetches the branch
checks/smoke --firmware ~/src/0700-AAM --ref <your-branch>
```

Fast mode needs these changes in the fork. Until they are merged into `KickEm-Tools/0700-AAM`
`main`, base your branch on `integration/standalone` or merge them yourself:

| Needed for | Branch (pull request) | Without it |
|---|---|---|
| building at all: reconstructed `AudIO_juce`, `AudIO_constants`, build fixes | `build/release-builds` (#1) | `kickem` stops before building |
| the panel's link: a real `juce_serialport`, `KICK_SERIAL_DEVICE` | `feature/serialport` (#15) | `kickem` stops before building |
| screen commands (`KICK_TEST_CONTROL`), audio probe and test input, `KICK_HARDWARE` | `feature/desktop-seams` (#2) | warning; checks that need them fail |
| the Starter Kit's samples (relative sample paths) | `fix/relative-sample-paths` (#7) | warning |
| SHIFT functions from a control surface | `fix/shift-state` (#16) | SHIFT + key behaviours do nothing (as on a unit today) |

`kickem` checks the fetched tree for these before building and names what is missing. Against
`KickEm-Tools/0700-AAM` `main` (`27194180`, equal to upstream) today it says the first two are
missing; with `--ignore-missing` the configure fails, because CPM tries to clone `AudIO` from the
placeholder server `https://example.com/your-git-server`.

## Status

| Phase (`/goal-standalone`) | What | State |
|---|---|---|
| 4 | This repository: licence, layout, the firmware fetch, protocol targets from the fetched ref, `protocol_roundtrip`, the Starter Kit, `kickem fetch` | done |
| 5a | The panel as its own program on the wire (pty, tcp, serial, throttle); `kickem run --mode fast`; the panel control channel and `kickem_ctl`; `checks/smoke` | done: smoke passes against `integration/standalone` |
| 5b | Ports of `sim_transport`, `sim_modes`, `sim_pads`, `sim_sampler_looper`, `sim_overlays`; the serial fault test | not started |
| 6 | Faithful mode: QEMU arm64 VM with the fork's `KT_Kick-linux-arm64`, panel on the VM's serial port | not started |
| 7 | Reusable CI workflow for forks (`test-firmware.yml`), panel release builds | not started |

## Layout

| Path | What |
|---|---|
| `panel/` | The front panel, `kickem-panel` (see `panel/README.md`) |
| `transport/` | pty, TCP and serial transports for the panel, and the throttle |
| `cmake/` | `KickemFirmware.cmake`: fetches the firmware by URL and ref, and exposes the protocol targets |
| `tools/protocol_roundtrip/` | Encodes and decodes packets with the fetched ref's `CPktMgr`, as the app's serial reader does |
| `modes/fast/` | How fast mode builds and runs a fork (it lives in `kickem`) |
| `modes/vm/` | Faithful mode (plan) |
| `checks/` | `smoke`, `lib/kickem_ctl`, `lib/wav_measure.py`, and the in-tree checks still to port |
| `content/StarterKit/` | The Starter Kit: synthesised samples and a demo project, CC0 |
| `kickem` | The launcher |
| `.github/workflows/` | Empty until phase 7 |

## Building the tools by hand

```sh
export MACOSX_DEPLOYMENT_TARGET=12.0                  # macOS: the firmware's JUCE needs it
cmake -S . -B build                                   # protocol_roundtrip only; fetches KickEm-Tools/0700-AAM main
cmake -S . -B build -DKICKEM_FIRMWARE_DIR=/path/to/0700-AAM -DKICKEM_PROTOCOL_JUCE=juce   # also the panel
cmake --build build && ./build/tools/protocol_roundtrip/protocol_roundtrip
```

See `cmake/README.md` for the options, including how the JUCE include in `dcl_contracts.h` is
satisfied.

## Content and branding

- **Starter Kit.** `content/StarterKit/` is synthesised from scratch by `make_starter_kit.py`
  (fixed seeds, Python standard library only) and is dedicated to the public domain under CC0 1.0,
  generator included. Its `LICENSE` sits beside it. Running the script reproduces the committed
  files byte for byte. `kickem` installs it into the build's data paths
  (`content/StarterKit/README.md`).
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
