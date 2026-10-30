# .github/workflows/

| Workflow | Runs on | What it does |
|---|---|---|
| `test-firmware.yml` | `workflow_call` (forks call it) | Builds a firmware ref with the emulator, boots it headless, runs checks, uploads the evidence, writes a job summary. README.md, "Add it to your fork's CI" |
| `self-test.yml` | push to `main`, pull requests, weekly, `workflow_dispatch` | The emulator's own CI: this commit through `test-firmware.yml` against `KickEm-Tools/0700-AAM` `main`, fast mode on `macos-15`, faithful mode on `ubuntu-24.04-arm` |
| `panel.yml` | tags `panel-v*`; pull requests touching the panel; `workflow_dispatch` | `kickem-panel` for macOS arm64 (the app bundle `KickEm.app`), Linux x86_64 and arm64. On a tag it publishes a GitHub Release; otherwise workflow artifacts only |

`.github/ci/` holds the two scripts `test-firmware.yml` runs: `run-checks.sh` (runs the checks one
after another, records `results.tsv`) and `summary.py` (the job summary).

## `test-firmware.yml` inputs

| Input | Default | |
|---|---|---|
| `firmware-repository` | the calling repository | `owner/name` |
| `firmware-ref` | the calling run's `github.sha` (a pull request's merge commit) | branch, tag or full SHA |
| `mode` | `fast` | `fast`: the desktop `Kick` app, panel over a pty. `vm`: the Linux arm64 `KT_Kick` in QEMU |
| `runner` | `macos-15` (fast), `ubuntu-24.04-arm` (vm) | fast also runs on `macos-14`, `macos-26`, `ubuntu-24.04`, `ubuntu-24.04-arm` |
| `checks` | `smoke` | space or comma separated; vm mode supports `smoke` only (the others report SKIP) |
| `emulator-ref` | `v1` | keep it equal to the `@ref` in `uses:` |
| `emulator-repository` | `KickEm-Tools/KickEmulator` | |
| `screen` | fast: `embedded` (kickem's default); vm: `window` | `embedded` puts the Kick's screen inside the panel; in vm mode that means QEMU's VNC server, which slows a TCG guest down several times |
| `ccache` | `true` | fast mode: compiler cache between runs |
| `artifact-name` | `kickem-evidence-<mode>-<runner>` | |

Output: `result` (`pass` / `fail`).

What the workflow does, step by step:

1. Checks out the emulator at `emulator-ref` and the firmware at `firmware-ref` (with the caller's
   token, so a private fork works), and points a fixed local branch, `kickem-ci`, at it. The
   build directories are keyed by URL and ref, so they stay the same from run to run and ccache
   hits.
2. Restores the CPM cache (JUCE, keyed by the JUCE tag and the firmware's JUCE patches) and, in
   fast mode, the ccache directory. Caches belong to the calling repository, and a pull request
   restores only what its base branch saved: run the workflow on pushes to `main` too.
3. macOS: makes sure there is an audio output device (hosted runners have "Apple Virtual Sound
   Device"; when one has none, it installs the BlackHole loopback driver from Homebrew). Linux:
   the build packages, Xvfb, and in fast mode PulseAudio with a null sink (hosted runners have
   no sound card); in vm mode QEMU and cloud-image-utils.
4. Fast: `kickem build`. VM: `modes/vm/artifact.sh --build` (Debian 12 arm64 in Docker), then the
   provisioned guest image, from the cache or provisioned under TCG.
5. `.github/ci/run-checks.sh` runs each check into `evidence/<check>/`.
6. `.github/ci/summary.py` writes the job summary: firmware and emulator commits, the runner,
   build / provisioning / job times, each check's result and notes, and what the mode (and the
   runner) cannot tell you.
7. Uploads `evidence/` (14 days).

Since this CI exists, changes to this repository go through pull requests.
