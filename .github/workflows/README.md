# .github/workflows/

Empty on purpose: CI comes in phase 7 of `/goal-standalone`.

- `test-firmware.yml` (`on: workflow_call`) is the reusable workflow forks will call. Its inputs
  are the firmware repository, the ref and the mode. It builds the fork, boots it headless, runs
  the smoke check, and uploads the log, the probe WAV and the screenshots. It is tagged `v1`.
- Panel release builds run on tags: macOS arm64, Linux x86_64 and arm64.

Until this workflow exists, changes are pushed straight to `main`; after that they go through
pull requests.
