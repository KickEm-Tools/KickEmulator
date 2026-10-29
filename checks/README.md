# checks/

Scripted checks that drive the emulator the way a person at the unit would: panel input over the
wire, the screen through the firmware's opt-in test-control module, and the audio through the
firmware's probe. Each check starts its own run with `kickem run --detach` (under the machine-wide
lock), prints `PASS` / `FAIL` lines, stops what it started, and leaves its evidence in an OUTDIR
(default `checks/out/<check>`, not committed). Each prints which mode ran and what that mode cannot
tell you.

Each check takes the same arguments: `[--mode fast] [--firmware URL|PATH] [--ref REF] [--no-build]
[--no-fetch] [OUTDIR]`. All of them are fast mode only so far (`--mode vm` exits 3, except `smoke`,
which runs in the VM too). Run times are after the build, on an Apple Silicon Mac.

| Check | What it proves | Run time | Needs in the fork (beyond `main` of 2026-10-29) |
|---|---|---|---|
| `smoke` | Boot and handshake over the wire; PUSH loads the Starter Kit; pad colours arrive; silent; PLAY four bars, STOP: PLAY / STOP LED packets, every whole second of the four bars above -40 dBFS, nothing above 0 dBFS, silent after STOP, the app alive. Prints the time from `kickem run` to sound | 20 s | `fix/ui-small` (#6: PUSH loads the selected demo) |
| `transport` | PLAY / STOP / PLAY START and BAR:BEAT:TICK, the first note on tick 0, gain staging (Pad 1 at -13.3 dBFS, centred), RECORD and OVERDUB (hits replayed one loop later within 16 ms, replace vs add, Timing Correct snapping), TAP TEMPO (100 BPM, loop 9.6 s), the metronome (clicks every 0.600 s, 2 kHz accent on beat 1) | 2 min 10 s | `fix/engine-gain-staging` (#4), `feature/metronome` (#12), `feature/key-handlers` (#13) |
| `modes` | Every function key and SHIFT function: the packet reaches the app, its LED comes back; P1-P4 on an empty project do not crash; TRACK MIX / PAD MIX levels heard; TRACK MUTE and PAD MUTE against the probe; NEXT SEQ; MASTER; TC moves Sequence 02's drums onto the grid; SAVE | 2 min 30 s | `fix/engine-gain-staging` (#4), `fix/ui-small` (#6), `feature/key-handlers` (#13) |
| `pads` | Banks, FULL / HALF LEVEL, 16 LEVELS, NOTE REPEAT and LATCH timing, NOTES pitches, ERASE + pad, BACK, COPY / ERASE in GRID, focus with slider / + / - / jog, P1-P4 per screen, MENU | 2 min | `feature/pad-modifiers` (#14, with #13), `fix/engine-gain-staging` (#4) |
| `sampler_looper` | SAMPLER records the input exactly (96000 frames of the -12 dBFS tone), preview and Pad 1 play it, SAMPLE EDIT's Discard trims it to 72000 frames, it reloads; the LOOPER loops every 384000 samples, periods sample-identical, OVERDUB adds | 1 min 50 s | nothing |
| `overlays` | Spinner, dialogs, nested dialogs and bubbles are overlays on the app's own window, within 1024x600, no other desktop window; a panel key reaches the app under a modal dialog; a touch on a list row selects it (read from the app's rendering) | 25-40 s | `feature/test-control-touch` (#18) for the list rows (`touch` needs the screen unlocked; it reports SKIP otherwise) |
| `serial_faults` | `/goal-serial` phase 5. Bad CRC, a frame split across two reads, two frames in one read, garbage between and before frames, an empty payload, closing and reopening the port. For each: good frames around it taken, no crash, a real key afterwards gets its LED back; after the reopen the app reconnects (handshake LIVE again) | 30 s | `fix/serial-robustness` (#17) |

The checks were ported from the in-tree simulator's `sim_*.sh` (`Kick/tools/` on
`rbdone/goal-simulator-controls`), with their assertions and thresholds. Each script's header lists
where it differs and why:
- `transport` reads the record mode from the engine's `Record:` lines;
- `modes` waits 2.5 s after muting every track, so a crash tail has ended;
- `pads` prints, rather than requires, the drop of the energy above 6 kHz after ERASE + Pad 3;
- `sampler_looper` asserts the results that were only printed before;
- `overlays` checks "inside the app's window", not "inside the simulated screen", and reads the
  touched list row from the rendering.

```sh
checks/smoke --ref integration/standalone                 # KickEm-Tools/0700-AAM, that branch
checks/transport --firmware /path/to/your/0700-AAM --ref my-branch [--no-build] [OUTDIR]
for c in smoke transport modes pads sampler_looper overlays serial_faults; do checks/$c --ref integration/standalone --no-build; done
```

**What fast mode cannot tell you**, for every check here: anything Linux-specific (systemd units,
scripts, device nodes, the Wi-Fi / Ethernet / CV / update screens), or how the unit times things.
Latency, CPU load and audio-callback timing are this computer's. The panel plays the
microcontroller's side of the protocol, not its firmware. A pseudo-terminal delivers each write as
one read, so `serial_faults` stages a split frame with two writes, where a UART splits anywhere.

## checks/lib

- **`kickem_ctl`** (successor of `sim_ctl.sh` / `sim_turn.sh`): talks to the running emulator found
  through `<run dir>/state.json` (`$KICKEM_RUN_DIR`, default `~/.cache/kickem/run`).
  - Panel commands (`press`, `down`, `up`, `hold`, `turn`, `slider`, `repeat`, `panelshot`, `link`)
    go to the panel's control channel and are acknowledged as `PanelControl: ok <n>` in the panel
    log. The panel sends real `CPktMgr` frames, so the app receives what a unit's microcontroller
    sends.
  - Screen commands (`tap`, `touch`, `click`, `clickat`, `list`, `shot`, `windows`, `status`) go
    to the firmware's `KICK_TEST_CONTROL` file and are acknowledged as `TestControl: ok <n>` in the
    app log. `shot OUT.png` also renders the panel to `OUT-panel.png`. `tap` is delivered to the
    touched component (works with the screen locked); `touch` is a real touch through the window,
    which a list's mouse listener hears too.
  - Fault injection (`raw HEX`, `split MS HEX1 HEX2`, `close`, `reopen`) goes to the panel's link.
  - `echo TEXT` writes a marker into both logs, so a check can cut either log at the same moment.
- **`common.sh`**: what every check shares: arguments, `start_emulator` / `stop_emulator`
  (`kickem run --detach --fresh --probe`), the probe clock, log markers.
- **`wav_measure.py`**: peak, RMS, onsets and dominant frequency of the probe WAV (needs numpy).
- **`row_highlight.py`**: which list row the app draws highlighted in a `shot` (standard library).

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

## Drift

When a check fails because the fork's `main` lacks a fix still in review, the check is right and
the fork is behind. The table above names the pull requests each check needs. Measured on 2026-10-29, against
KickEm-Tools/0700-AAM `main` at `551de935` (PRs #1, #2, #3, #5, #7-#11 and #15 merged), against
`main` + only the pull requests listed, and against `integration/standalone` at `2d2c9462` (all open
pull requests):

| Check | `main` | Why it fails there | `main` + | Result | `integration/standalone` |
|---|---|---|---|---|---|
| `smoke` | FAIL (4) | PUSH loads an empty project, not the Starter Kit (#6); with #6 alone the master clips at +2.1 dBFS (a loaded track's level ignored, #4) | #6, #4 | SMOKE OK | SMOKE OK |
| `transport` | FAIL | the Starter Kit does not load (#6) | #6, #4, #12, #13 | TRANSPORT OK | TRANSPORT OK |
| `modes` | FAIL (11 of 50) | AUTO and MASTER have no handler (#13); the LOOP LED stays on (#6); the Starter Kit does not load (#6), so the audio, SAVE and TC rows fail; MIX levels `gap` | #4, #6, #13 | MODES OK, 50 of 50 | MODES OK |
| `pads` | FAIL | the Starter Kit does not load (#6) | #6, #14 (with #13), #4 | PADS OK | PADS OK |
| `sampler_looper` | SAMPLER_LOOPER OK | | | | SAMPLER_LOOPER OK |
| `overlays` | FAIL (1) | no `touch` in the test control (#18) for the list rows | #18 | OVERLAYS OK (list rows SKIP: the screen was locked) | OVERLAYS OK (list rows SKIP: locked; both passed earlier the same day, unlocked) |
| `serial_faults` | FAIL | the Starter Kit does not load (#6); without #17 five cases fail (split frame, garbage between and before frames, empty payload, close and reopen) | #6, #17 | SERIAL_FAULTS OK | SERIAL_FAULTS OK |
