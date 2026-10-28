# modes/fast/: the fork's desktop app against the panel

Fast mode builds the fork's `Kick` app for the desktop (macOS; Linux not yet tried) from the
fork's own source, connects it to the panel over a pty, and runs it on your machine: quick
rebuilds, a debugger, scripted checks. It lives in the launcher, `./kickem run --mode fast` (the
default), which does this:

1. **Fetch** the ref into `~/.cache/kickem/firmware/<repo>-<hash>/<ref>-<hash>` (shallow).
2. **Check** that the ref has what fast mode needs (below). A missing serial port stops it with a
   message naming the branch; the others are warnings.
3. **Build**, out of tree in `~/.cache/kickem/build/<repo>-<hash>/<ref>-<hash>/`:
   - `kick/`: `cmake -S <checkout>/Kick -DCMAKE_BUILD_TYPE=Release -DTIMESTAMP=OFF
     -DCPM_SOURCE_CACHE=...`, then `cmake --build --target Kick`. On macOS with
     `MACOSX_DEPLOYMENT_TARGET=12.0` in the environment (JUCE's `juceaide` needs it).
   - `panel/`: this repository with `-DKICKEM_FIRMWARE_DIR=<checkout> -DKICKEM_PROTOCOL_JUCE=juce`,
     target `kickem-panel`, so the panel uses that ref's `PktMgr` and control ids.
   - CPM downloads (JUCE) go to `$CPM_SOURCE_CACHE`, else `~/.cache/kickem/cpm`. Point it at an
     existing cache (a firmware checkout's `.3rdPartySourceCache`) to skip the download.
   - The build log is `build.log` there; `build-times.json` records each build's durations.
4. **Install the Starter Kit** into `kick/`, where the app resolves its data (its working
   directory on macOS): `assets/StarterKit`, `data/samplesKick/StarterKit`, `demos/Starter Kit`,
   `assets/Default/recent_project/Starter Kit.png`. With `--fresh`, `demos/` and `templates/` are
   first restored from the checkout and `prefs/` is dropped, so saves and recent projects from an
   earlier run do not leak in.
5. **Run**, holding the machine-wide lock (`/tmp/kt_kickem.lock`, the in-tree `sim_launch.sh`
   format; it also waits while any `Kick` or `KT_KickEM` runs without it, since `Kick` is
   single-instance):
   - the panel, `kickem-panel --transport pty --control <run>/panel.ctl`, which logs
     `SimSerial: listening on /dev/ttysNNN`;
   - `Kick`, from `kick/`, with `KICK_SERIAL_DEVICE=<that pty>`, `KICK_TEST_CONTROL=<run>/test.ctl`,
     `KICK_AUDIO_INPUT=silence` (unless set), `KICK_AUDIO_PROBE=<wav>` with `--probe`, and
     `KICK_HARDWARE=none` on Linux (so the app never runs `ifconfig` or `timedatectl` on your
     computer);
   - then it waits for `Send Comm Live` in the app's log and `SimSerial: LIVE` in the panel's.
6. In the foreground it runs until either program quits or Ctrl-C, then stops both. `--detach`
   leaves them running for `./kickem stop`. The run's files are in `--out` (default
   `~/.cache/kickem/run`): `kick.log`, `panel.log`, the two control files and `state.json`.

**What the fork must have** (topic branches in `KickEm-Tools/0700-AAM`, see
`docs/STANDALONE_SPLIT.md` on the work branch):

| Needed for | Branch | Without it |
|---|---|---|
| building at all (reconstructed `AudIO_juce`, build fixes) | `build/release-builds` | stops before building |
| the panel's link (a real `juce_serialport`, `KICK_SERIAL_DEVICE`) | `feature/serialport` | stops before building |
| screen commands (`KICK_TEST_CONTROL`), the audio probe and test input, `KICK_HARDWARE` | `feature/desktop-seams` | warning; checks that need them fail |
| the Starter Kit loading (relative sample paths) | `fix/relative-sample-paths` | warning; the Starter Kit's samples are not found (expected from the code, not tried) |
| SHIFT functions from a control surface | `fix/shift-state` | SHIFT + pad etc. do nothing, as on a unit today |

`KickEm-Tools/0700-AAM` branch `integration/standalone` merges every open pull request, for
running everything at once until they are merged into `main`.

**Linux.** The app resolves its data from absolute paths there (`/app/assets`, `/data/sounds`,
`/home/Kick/...`), so fast mode on Linux also needs those paths to point at the build. This is not
automated or tried yet.

**Cannot tell you:** anything Linux-specific. That covers systemd units, scripts, device nodes,
and the Wi-Fi / Ethernet / CV / firmware-update screens, which show as "not available on this
system". Timing, latency and CPU load are your computer's, not the unit's. Use faithful mode for
those.
