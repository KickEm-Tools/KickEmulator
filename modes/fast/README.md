# modes/fast/: the fork's desktop app against the panel (phase 5)

Fast mode builds the fork's `Kick` app for the desktop (macOS, Linux) from the fork's own source.
It connects the app to the panel over a pty and runs it on your machine, so you get quick
rebuilds, a debugger and scripted checks.

**Nothing here yet.** Phase 5 adds:

- **Build.** Configure the fetched tree's `Kick/` CMake project, out of tree, with
  `MACOSX_DEPLOYMENT_TARGET=12.0` on macOS and the CPM flags the release needs
  (`CPM_AudIO_SOURCE`, `CPM_COM_PROTOCOL_SOURCE` pointing at the in-tree copies, unless the fork
  defaults them). Then build the `Kick` target.
- **Install the content.** Copy `content/StarterKit/` into the build's data paths
  (`content/StarterKit/README.md` has the map). With `--fresh`, restore `demos/` and
  `templates/` first, as `KICK_SIM_FRESH` did.
- **Run.** Take the machine-wide lock. Start the panel (`--transport pty`), then the app with:
  - `KICK_SERIAL_DEVICE=<pty slave>`;
  - `KICK_TEST_CONTROL=<file>` (screen commands for the checks);
  - `KICK_AUDIO_PROBE=<wav>` and `KICK_AUDIO_INPUT=silence` when a check asks for them;
  - `KICK_HARDWARE=none` on Linux, so the app never runs `ifconfig` or `killall` on your computer.

  Wait for the control-surface handshake in both logs.

**What the fork must have** (topic branches in `KickEm-Tools/0700-AAM`, see
`docs/STANDALONE_SPLIT.md` on the work branch):

- `build/release-builds`;
- `feature/serialport` (`KICK_SERIAL_DEVICE` and a real POSIX `juce_serialport`);
- `feature/desktop-seams` (`KICK_TEST_CONTROL`, `KICK_AUDIO_*`, `KICK_HARDWARE`, overlays);
- `fix/relative-sample-paths` (the Starter Kit loads).

A fork without them gets a clear message naming what is missing, not a hang.

**Cannot tell you:** anything Linux-specific. That covers systemd units, scripts, device nodes,
and the Wi-Fi / Ethernet / CV / firmware-update screens, which show as "not available on this
system". Use faithful mode for those.
