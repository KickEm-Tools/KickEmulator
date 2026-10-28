# modes/vm/: the fork's Linux arm64 build in QEMU (phase 6)

Faithful mode runs the fork's **built** `KT_Kick` for Linux arm64 inside a QEMU arm64 virtual
machine. The artifact is `KT_Kick-linux-arm64` from the fork's CI, or one built locally in an arm64
Debian container. The panel attaches to the VM's serial port. This exercises the device's Linux
code paths, files, scripts and services.

**Nothing here yet.** Phase 6 (`/goal-vm` phases 1-4) adds:

- **Image.** A stock Debian arm64 cloud image, downloaded into `$KICKEM_CACHE/vm/` on first use
  and checksum-verified. It is **never committed** (`.gitignore` excludes images and downloads).
  It is customised with cloud-init: the artifact, the Starter Kit, ALSA `snd-aloop`, and a serial
  console on a second UART.
- **Accelerator.** `hvf` on Apple Silicon. TCG (emulated) on x86 runners; boot and run times are
  measured and documented for both.
- **Panel.** The panel attaches through QEMU's serial backend: a pty on macOS and Linux, TCP for
  Windows (`transport/`).
- **Audio** is asserted through the firmware's `KICK_AUDIO_PROBE` or through `snd-aloop` capture
  in the guest, so no firmware change is needed.
- **Screen touches** go through QMP `input-send-event` with absolute axes on a `virtio-tablet`.
  This works headless.
- **Checks.** `checks/smoke --mode vm` passes. The Buildroot image (`/goal-vm` phase 5) stays
  optional; it is done only if the stock image misses device behaviour that matters.

**Cannot tell you:** latency, CPU headroom on the A53 cores, the codec, the GPU driver, or the
control-surface microcontroller's own behaviour (the panel plays its side of the protocol from
the firmware's headers, not from its firmware, which is in no repository).
