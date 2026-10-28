# checks/

**Not ported yet.** These scripts are the in-tree simulator's checks and helpers, copied unchanged
from `Kick/tools/` on `rbdone/goal-simulator-controls` at `6b6024ce`. They still expect the
firmware tree around them: `$TOOLS/../build/KT_KickEM.app`, `Kick/demos`, `Kick/templates`. They
also still expect the `SIM_BUILD` app, with its `KICK_SIM_*` variables and the in-process
`KICK_SIM_CONTROL` channel. **They do not run from this repository.** They are here as the
specification for phase 5, which ports them.

| Script | What it checks or does (DEVELOPMENT.md on the work branch) | Phase 5 |
|---|---|---|
| `sim_smoke.sh` | Boot, PUSH loads the Starter Kit, four bars play, LED feedback arrives (4.10) | `checks/smoke`, fast and faithful mode |
| `sim_transport.sh` | Transport keys, BAR:BEAT:TICK, gain staging, RECORD / OVERDUB, TAP TEMPO, metronome (4.7) | ported, fast mode |
| `sim_modes.sh` | Every mode key and SHIFT function, mixers, NEXT SEQ (4.8) | ported, fast mode |
| `sim_pads.sh` | Pads, FULL / HALF / 16 LEVELS, NOTE REPEAT, LATCH, ERASE + pad, encoders, slider (4.9) | ported, fast mode |
| `sim_sampler_looper.sh` | Sampler record / preview / save / trim, looper record / loop / overdub (4.6) | ported, fast mode |
| `sim_overlays.sh` | Popups, spinner and bubbles inside the screen, list-row touches (4.1) | ported, fast mode |
| `sim_launch.sh`, `sim_quit.sh` | Start and stop the simulator under the machine-wide lock `/tmp/kt_kickem.lock` (`KICK_SIM_LOCK`) | become `kickem run` and `kickem stop`. The same lock design: `mkdir` lock, owner pid inside, stale owner reclaimed |
| `sim_ctl.sh`, `sim_turn.sh` | Client for the in-app `KICK_SIM_CONTROL` channel | split. Panel commands go to the panel's control channel, screen commands to the firmware's `KICK_TEST_CONTROL` |
| `sim_press.sh`, `sim_click.sh`, `sim_shot.sh`, `sim_ax.swift` | macOS accessibility and window-server helpers | dropped. They need an unlocked, visible screen, and the wire plus test-control replace them |
| `wav_measure.py` | Peak, RMS, onsets and dominant frequency of the probe WAV (needs numpy) | kept, moves to `checks/lib/` |

## What porting means

- **Panel input** (keys, pads, encoders, slider) goes through the panel's control channel. The
  panel sends real `CPktMgr` frames over the transport, so the app receives exactly what a unit's
  microcontroller would send.
- **Screen input and state** (`tap`, `click NAME`, `status`, `shot`) go through the firmware's
  opt-in `KICK_TEST_CONTROL=<file>` module in fast mode, and through QEMU input injection (QMP
  `input-send-event`) in faithful mode.
- **Audio** comes from `KICK_AUDIO_PROBE` (was `KICK_SIM_AUDIO_PROBE`) and `KICK_AUDIO_INPUT` (was
  `KICK_SIM_AUDIO_INPUT`). In the VM it comes from the probe or ALSA `snd-aloop`.
- **Results.** Each check prints which mode ran and what that mode cannot tell you (see the top
  README). When a check fails because the fork's `main` lacks a fix still in review, it says so
  and names the fix branch.
