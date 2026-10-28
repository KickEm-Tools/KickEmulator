# checks/

Scripted checks that drive the emulator the way a person at the unit would: panel input over the
wire, the screen through the firmware's opt-in test-control module, and the audio through the
firmware's probe. Each check starts its own run with `kickem run --detach` (under the machine-wide
lock), prints `PASS` / `FAIL` lines, stops what it started, and leaves its evidence in an OUTDIR
(default `checks/out/<check>`, not committed). Each prints which mode ran and what that mode cannot
tell you.

| Check | What | State |
|---|---|---|
| `smoke` | Boot, handshake over the wire, PUSH loads the Starter Kit, pad colours arrive, silent; PLAY four bars, STOP: PLAY / STOP LED packets, every whole second of the four bars above -40 dBFS, nothing above 0 dBFS, silent after STOP, the app still running. Also prints the time from `kickem run` to sound | ported (`--mode fast`; `vm` in phase 6) |
| `sim_transport.sh` | Transport keys, BAR:BEAT:TICK, gain staging, RECORD / OVERDUB, TAP TEMPO, metronome (work-branch DEVELOPMENT.md 4.7) | **not ported yet** (5b) |
| `sim_modes.sh` | Every mode key and SHIFT function, mixers, NEXT SEQ (4.8) | **not ported yet** (5b) |
| `sim_pads.sh` | Pads, FULL / HALF / 16 LEVELS, NOTE REPEAT, LATCH, ERASE + pad, encoders, slider (4.9) | **not ported yet** (5b) |
| `sim_sampler_looper.sh` | Sampler record / preview / save / trim, looper record / loop / overdub (4.6) | **not ported yet** (5b) |
| `sim_overlays.sh` | Popups, spinner and bubbles inside the screen, list-row touches (4.1) | **not ported yet** (5b) |
| (new) `serial_faults` | `/goal-serial` phase 5: bad CRC, split frame, two frames in one read, garbage, close and reopen | **not written yet** (5b) |

The `sim_*.sh` scripts are the in-tree simulator's checks, copied unchanged from `Kick/tools/` on
`rbdone/goal-simulator-controls` at `6b6024ce`. They still expect the `SIM_BUILD` app and do not
run from here; they are the specification for their ports.

```sh
checks/smoke --ref integration/standalone                 # KickEm-Tools/0700-AAM, that branch
checks/smoke --firmware /path/to/your/0700-AAM --ref my-branch [--no-build] [OUTDIR]
```

## checks/lib

- **`kickem_ctl`** (successor of `sim_ctl.sh` / `sim_turn.sh`): talks to the running emulator found
  through `<run dir>/state.json` (`$KICKEM_RUN_DIR`, default `~/.cache/kickem/run`).
  - Panel commands (`press`, `down`, `up`, `hold`, `turn`, `slider`, `repeat`, `panelshot`, `link`)
    go to the panel's control channel and are acknowledged as `PanelControl: ok <n>` in the panel
    log. The panel sends real `CPktMgr` frames, so the app receives what a unit's microcontroller
    sends.
  - Screen commands (`tap`, `click`, `clickat`, `list`, `shot`, `windows`, `status`) go to the
    firmware's `KICK_TEST_CONTROL` file and are acknowledged as `TestControl: ok <n>` in the app
    log. `shot OUT.png` also renders the panel to `OUT-panel.png`.
  - `echo TEXT` writes a marker into both logs, so a check can cut either log at the same moment.
- **`wav_measure.py`**: peak, RMS, onsets and dominant frequency of the probe WAV (needs numpy).

## Log names (the firmware's, since `feature/desktop-seams`)

| In-tree simulator | Now |
|---|---|
| `SimControl:` (both halves) | `PanelControl:` (panel log), `TestControl:` (app log) |
| `SimProject: loaded <path>` | `Project: loaded <path>` |
| `SimUnavailable:` | `FeatureUnavailable: <feature> shown\|hidden` |
| `SimNote:` (`KICK_SIM_LOG_NOTES`) | `Note: track ...` (`KICK_LOG_NOTES=1`) |
| `KICK_SIM_AUDIO_PROBE`, `KICK_SIM_AUDIO_INPUT` | `KICK_AUDIO_PROBE`, `KICK_AUDIO_INPUT` |
| `SimSurface:` | unchanged, in the panel log (`KICK_SIM_LOG_SURFACE=1`) |
| (none) | `SimSerial:` (panel log), `InputEvents:` / `Send Comm Live` (app log) |

## What porting the rest means

- Replace `sim_launch.sh` / `sim_quit.sh` with `kickem run --detach --fresh --out OUT/run` and
  `kickem stop --out OUT/run`, and `sim_ctl.sh` with `kickem_ctl` (`KICKEM_RUN_DIR=OUT/run`).
- Grep the app log for engine and screen lines, and the panel log for LED and pad packets
  (`SimSurface:`); the markers from `kickem_ctl echo` are in both.
- `TestControl` `tap` / `click` deliver to the touched component only, so a parent's mouse
  listener (the `KickTableView` canvas) does not hear them: list-row selection by touch needs a
  check of its own.
- When a check fails because the fork's `main` lacks a fix still in review, say so and name the
  fix branch; run it against that branch (or `integration/standalone`) to show the check is right.
