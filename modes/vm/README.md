# modes/vm/: faithful mode, the fork's Linux arm64 build in QEMU

Faithful mode runs the fork's **built** `KT_Kick` for Linux arm64 in a QEMU arm64 virtual machine,
started the way the unit starts it (`ktkick.service` → `kt_kick.sh` → `/usr/bin/KT_Kick`, under
Weston with Xwayland), with the panel on the VM's serial port. It exercises the device's Linux
code: the `/app`, `/data/sounds` and `/home/Kick` paths, the real `juce_serialport` on a real UART
driver, and the Ethernet, Wi-Fi, clock and backlight code that fast mode leaves out.

```sh
./kickem run --mode vm --ref integration/standalone          # or modes/vm/run.sh --ref ...
checks/smoke --mode vm --ref integration/standalone
```

**What it cannot tell you:** latency, CPU headroom on the A53 cores, the codec, the GPU driver,
or the control-surface microcontroller's own behaviour (the panel plays its side of the protocol
from the firmware's headers; the microcontroller's firmware is in no repository). The VM is QEMU's
generic `virt` board, not an i.MX 8M Mini: the same app and the unit's service files, but a
different kernel, device tree and drivers (PL011 instead of the i.MX UART, virtio-gpu and llvmpipe
instead of the Vivante GPU, ALSA loopback instead of the codec), and a Debian userland instead of
the Buildroot image.

## Needs

- `qemu-system-aarch64` with EDK2 firmware (macOS: `brew install qemu`, tested with QEMU 11.1.2;
  Debian/Ubuntu: `qemu-system-arm qemu-efi-aarch64`).
- Docker, to build the artifact (unless you pass `--artifact`). Docker Desktop on Apple Silicon
  runs the arm64 container natively.
- `ssh`, `scp`; on macOS `hdiutil` (built in) makes the cloud-init seed, elsewhere `cloud-localds`,
  `genisoimage`, `mkisofs` or `xorrisofs`.
- About 3 GB of disk in `~/.cache/kickem/vm` and Docker (see [What is left on disk](#what-is-left-on-disk)).

## How it works

| Step | What happens | Where |
|---|---|---|
| Artifact | `KT_Kick-linux-arm64` (`KT_Kick`, `assets/`, `templates/`, `demos/`, `manifest.json`, `shared-libraries.txt`: the layout of the fork's CI). Built from the fetched ref in a Debian 12 arm64 container (`artifact.sh --build`), or given with `--artifact` (`artifact.sh --ci` downloads one from a CI run) | `artifact.sh`, `builder/Dockerfile` |
| Image | The stock Debian 12 (bookworm) `generic` arm64 cloud image (the `genericcloud` kernel lacks the sound, DRM and hwsim modules), SHA-512 checked, and QEMU's EDK2 `edk2-aarch64-code.fd`, both copied into `~/.cache/kickem/vm` | `kickem_vm.py` |
| Provision (once) | cloud-init from a NoCloud seed: user `kickem` with the cache's SSH key, the app's runtime libraries, Weston + Xwayland, ALSA utils, the tools the app's Linux code runs (`ifconfig`, `killall`, `iwlist`, `wpa_supplicant`, `dhclient`, `udhcpc`, `timedatectl`), hostapd, the console moved to `hvc0`, NIC names, `snd-aloop` / `mac80211_hwsim` / 9p modules, the device directories. It powers off; the result is one compressed qcow2 | `guest/user-data` |
| Boot (every run) | A fresh copy-on-write overlay of the provisioned image (runs never leak state into each other), QEMU `virt`, `-accel hvf` (Apple Silicon) / `kvm` / `tcg`, 4 CPUs, 2 GB | `kickem_vm.py boot` |
| Deploy | The artifact to `/usr/bin/KT_Kick` and `/app/{assets,templates,demos}`; the Starter Kit to `/app/assets/StarterKit`, `/data/sounds/StarterKit`, `/app/demos/Starter Kit`; **the fork's own** `ktkick.service`, `kt_kick.sh`, `weston@.service`, `weston.ini`, `71-weston-drm.rules`, `asound.conf`, `profile.d/weston.sh` from its `rootfs_overlay`; a `ktkick.service.d/kickem.conf` drop-in with the environment below; then `weston@root` | `guest/deploy.sh` |
| Panel | `kickem-panel --transport serial:<pty>` (or `tcp:127.0.0.1:<port>`) on the guest's UART | `kickem` |
| Start | `systemctl start ktkick.service`; the launcher waits for `Send Comm Live` (app) and `SimSerial: LIVE` (panel) | `kickem` |
| Stop | `systemctl stop ktkick` (finishes the probe WAV), copy back the probe WAV, `journalctl`, `/home/Kick/logs` and a listing of the device paths; power off; delete the overlay | `kickem_vm.py stop` |

`ktkick.service` is started after the deploy, not at boot, so that the panel is on the wire
before the app opens its port (the unit's microcontroller is always there). Everything else on the
unit's start-up path (the service, `kt_kick.sh` and its 15 s sleep, `weston@root` from the udev
rule's unit) is the fork's own file.

### The VM's hardware

| Unit | VM | Notes |
|---|---|---|
| i.MX UART `/dev/ttymxc0`, 230400 8N1, to the panel MCU | PL011 `/dev/ttyAMA0`; QEMU `-serial chardev:panel` on a pty (`-chardev pty`) or a TCP socket (`--vm-serial tcp`, for Windows hosts) | The app gets `KICK_SERIAL_DEVICE=/dev/ttyAMA0`. No udev symlink to `/dev/ttymxc0`: the variable is the firmware's documented way to name the port, and the app's log then says which path it opened. Nothing else talks on ttyAMA0: the kernel console and getty are on the virtio console `hvc0` (`<run>/boot.log`), the machine runs with `acpi=off` so no SPCR console appears on the UART, and GRUB is off serial |
| 1024x600 LCD, Vivante GPU | `virtio-gpu-pci,xres=1024,yres=600`. Weston's DRM backend picks 1024x600 as preferred and current; GL by llvmpipe | |
| Touch screen | `usb-tablet` (absolute pointer). A touch is QMP `input-send-event` with `abs` axes, then a left-button press and release; with the screen embedded, the panel's clicks arrive as VNC pointer events on the same tablet | `kickem_ctl tap X Y`, `kickem_ctl screentouch X Y` |
| Screen in the panel | QEMU's VNC server on a unix socket (`--screen embedded`), raw encoding to the panel's RFB client | `kickem run --mode vm` (default) |
| Screen capture | QMP `screendump` (PNG) of the VM's display | `kickem_ctl shot OUT.png` |
| Codec (ALSA card 0) | `snd-aloop` as card 0, so the unit's `asound.conf` (`card0`, `S32_LE`, 48 kHz through `plug`) works unchanged; JUCE opens `default`. `--sound` adds a virtio sound card (card 1) that plays to this computer's output | Sound is asserted with the firmware's `KICK_AUDIO_PROBE`, not by listening |
| Ethernet `eth0` | A virtio NIC named `eth0` (by MAC) on its own isolated user-mode network. The app configures it (`ifconfig eth0 192.168.0.1`) as on the unit | SSH has a second NIC, `mgmt0`, forwarded to `127.0.0.1` only |
| Wi-Fi `wlan0` | `mac80211_hwsim` with two radios: `wlan0` for the app, `wlan1` running hostapd as an open access point `KickEm-VM` | |
| SD card, USB stick | `--disk IMG` (repeatable) attaches USB mass storage | The device's storage scripts and overlay mounts are not installed |
| CV / gate (I2C DAC), camera | absent (`/dev/i2c-3` missing, so the hardware probe reports `cv` unavailable) | |

### The app's environment in the guest

`KICK_SERIAL_DEVICE=/dev/ttyAMA0`, `KICK_AUDIO_PROBE=/var/tmp/kickem/probe.wav` (with `--probe`),
`KICK_AUDIO_INPUT=silence`, `KICK_TEST_CONTROL=/mnt/kickem/test.ctl`, `DISPLAY=:0`,
`XDG_RUNTIME_DIR=/run/user/0`. **`KICK_HARDWARE` is not set**, so the app's hardware probe finds
what the VM has and its real Linux code runs. The app's stdout and stderr go to
`/mnt/kickem/kick.log`. `/mnt/kickem` is the run directory on the host, shared over 9p
(`virtio-9p-pci`, `security_model=none`), so `kickem_ctl` and the checks read the log and write the
test-control file exactly as in fast mode.

### Why a locally built artifact on Debian 12, not the CI artifact

The fork's CI builds `KT_Kick-linux-arm64` natively on `ubuntu-24.04-arm`. That binary needs
glibc 2.39 and `libmvec.so.1`, and Debian 12 has glibc 2.36. In the guest it fails at once:

```
/tmp/KT_Kick.ci: error while loading shared libraries: libmvec.so.1: cannot open shared object file: No such file or directory
```

`artifact.sh --ci` checks the glibc version and says so. There are two ways out: a Debian 13
(trixie, glibc 2.41) guest for the CI artifact, or a Debian 12 guest with the artifact built in a
Debian 12 container. This mode does the second. Debian 12 is the stock image the plan names, a
container build is reproducible on any Docker host, and the binary then needs only glibc 2.35.
The fork's CI could build its arm64 job in a `debian:bookworm` container, and its artifact would
then run here directly.

## Usage

```sh
./kickem run --mode vm [--firmware URL|PATH] [--ref REF] [--artifact TARBALL] [--accel hvf|kvm|tcg]
             [--display] [--vm-serial pty|tcp] [--sound] [--disk IMG]... [--probe OUT.wav]
             [--detach] [--out DIR] [--no-build] [--no-start-delay]
./kickem stop                                   # stops the app, copies back logs and the probe, powers off
modes/vm/run.sh ...                             # the same as kickem run --mode vm
modes/vm/run.sh provision [--force]             # only download and provision the image
modes/vm/artifact.sh --build SRC OUTDIR         # the artifact, built in a Debian 12 arm64 container
modes/vm/artifact.sh --ci OWNER/REPO RUN|BRANCH OUTDIR
modes/vm/kickem_vm.py ssh  --out RUN [CMD]      # a shell in the running guest (user kickem, sudo)
modes/vm/kickem_vm.py tap  --out RUN X Y        # a touch on the VM's screen (QMP)
modes/vm/kickem_vm.py shot --out RUN OUT.png    # a screendump (QMP)
checks/lib/kickem_ctl tap X Y | shot OUT.png    # the same through the checks' tool (vm mode)
```

- `--screen embedded` (the default of `kickem run`) starts QEMU with its VNC server on a unix
  socket (`-vnc unix:<run>/vnc.sock`, or a short path under `/tmp` when that is too long) and no
  display window; the panel shows the VM's screen in its cut-out and sends clicks and drags to
  QEMU, which turns them into absolute events on the `usb-tablet` (Weston, Xwayland, the app).
  Nothing in the firmware is involved. `--screen window` is the previous behaviour:
  `--display` opens the VM's screen in a window (`-display cocoa` on macOS), else it is headless.
- `--no-start-delay` starts `/usr/bin/KT_Kick` directly. By default the unit's own `kt_kick.sh`
  runs, and it sleeps 15 s before starting the app, as on the unit.
- Every other `kickem_ctl` command (`press`, `turn`, `echo`, `status`, `list`, `click`, ...) works
  as in fast mode: panel commands go over the VM's serial port, screen commands through the shared
  test-control file.
- Run files are in `--out` (default `~/.cache/kickem/run`): `kick.log`, `panel.log`, `boot.log`
  (the guest's console), `qemu.log` (the QEMU command line), `state.json`, and after `stop`,
  `guest/` (`journal.txt`, `services.txt`, `home-Kick-logs/`, `device-paths.txt`).
- One emulator per machine: `run` holds `/tmp/kt_kickem.lock` from boot to power-off, with the
  QEMU pid as the owner. Provisioning and the Docker build do not take the lock.

## Timing

Measured on 2026-10-29 on an Apple M4 Max (16 cores, 128 GB, macOS 26.5), QEMU 11.1.2, against
`integration/standalone` at `cc5fa831`, by `checks/smoke --mode vm`. The guest has 4 vCPUs and
2 GB. Times while waiting for the machine lock (another emulator running) are left out.

| Step | hvf (hardware-accelerated) | TCG on the same Mac (emulated, `--accel tcg`, `-cpu cortex-a53`) |
|---|---|---|
| Provision the image (once: cloud-init, about 200 packages, compress) | 95-112 s | not measured (provisioning uses the default accelerator) |
| Artifact: cold build in Docker (empty build tree: all 51 objects at -j3, LTO link; JUCE seeded from the host's CPM cache, the image's apt layer from Docker's build cache) | 244 s | the same (Docker, not the VM) |
| Artifact: rebuild after a firmware change of a few files | 168-204 s (the LTO link is most of it) | the same |
| Boot: QEMU start to SSH | 12.3 s | 19.5 s |
| Deploy: artifact, Starter Kit, service files, Weston | 1.1 s | 15.3 s |
| `ktkick.service` started to handshake LIVE (includes `kt_kick.sh`'s 15 s sleep) | 16.1 s | 22.4 s |
| Handshake, app's connect frame to LIVE | 0.3 s, 1 try | 5.5 s, 4 tries |
| `Project: loaded` to the load's pad colours | under 2 s | 9.5 s |
| PLAY to the first loud second in the probe log | 1.1 s | 0.7 s |
| `kickem run` to LIVE, artifact and panel already built | 30.4 s | 59.2 s |
| Whole smoke (boot, four bars at 120 BPM, stop, copy back, power off), built | about 51 s | about 97 s |

TCG on Apple Silicon is quick because the host cores are fast; it is an emulation-speed number,
not a CI number. **x86 Linux runners (TCG), estimated, not measured here:** a GitHub-hosted
`ubuntu-24.04` runner (4 vCPUs, each well under half an M4 core for TCG work) should take roughly
2-4 times the TCG column: boot 40-80 s, deploy 30-60 s, LIVE 2-4 min after `kickem run`, the whole
smoke 4-7 min, plus the artifact build (on such a runner, an arm64 Docker build is itself emulated
by binfmt/QEMU and takes far longer; use the fork's CI artifact built for the guest instead). The
audio engine has to keep up in real time for the probe check; under slow TCG it may not, and the
check would then fail for speed, not for a bug. Hosted arm64 runners (`ubuntu-24.04-arm`) have no
KVM, so they run TCG too, but without cross-architecture translation.

## Device Linux paths: what ran for real

Observed in the VM on 2026-10-29 with `KICK_HARDWARE` unset (logs: `kick.log`, `guest/journal.txt`;
screens: QMP screendumps):

| Path | What ran | Result |
|---|---|---|
| Serial | `InputEvents: control surface on /dev/ttyAMA0 (KICK_SERIAL_DEVICE), 230400 8N1, no flow control: open`; the real `juce_serialport` on the PL011 driver | Handshake LIVE in 0.3 s (hvf); every smoke frame both ways, no junk or bad-CRC frames in `panel.log` |
| Display | Weston DRM backend on virtio-gpu, desktop shell, Xwayland `:0`, the unit's `weston.ini` (its `use-g2d=1` is ignored) | The app fills the 1024x600 output at (0,0); QMP touches reach it through libinput, Weston and Xwayland (`XInput: No touch devices found`: the tablet is a pointer) |
| Audio | `AudioDevice: using 'default' at 48000 Hz, device buffer 480, engine block 480` through the unit's `asound.conf` to the loopback card | Probe as in fast mode (peak -2.9 dBFS); JACK is tried first and is absent (`jack server is not running`), as it may be on the unit |
| Paths | `/app/demos/Starter Kit` loads (`Project: loaded /app/demos/Starter Kit`); the File Browser lists Internal = `/home/Kick/projects` and Factory Sample Library = `/data/sounds` (with `StarterKit`) | SAVE of the loaded demo writes `Starter Kit.kickPrj` and the sequences' `.mid` files back into `/app/demos/Starter Kit/`; preferences go to `/home/Kick/prefs/preferences/preferences.settingsprj`. `/home/Kick/logs` stays empty: the app writes no log file there |
| Start-up | `timedatectl set-timezone Asia/Kolkata` (the default preference) | Ran for real: `systemd-timedated: Changed time zone to 'Asia/Kolkata' (IST)`. The guest's clock shows IST from then on |
| Backlight | `echo 100 > /sys/devices/platform/backlight/backlight/backlight/brightness` | `sh: 1: cannot create /sys/devices/platform/backlight/...: Directory nonexistent` (no backlight device in the VM) |
| Ethernet | At start-up `killall udhcpc` (`udhcpc: no process found`) and `ifconfig eth0 192.168.0.1` | `eth0` is 192.168.0.1/24 in the guest; the Ethernet tab shows ENABLED, 192.168.0.1, 255.255.255.0, gateway and DNS 0.0.0.0, and the log gets `Failed to get subnet mask` on every refresh although the mask is shown |
| Wi-Fi | The Wi-Fi tab (the real one, not the "not available" panel: `wlan0` exists); ENABLED, ScanWifi, connect | ENABLED only stores the setting: `SettingsWifi::handleEnabledClick()`, which runs `ip link set wlan0 up`, is never called, so `wlan0` stays down and `iwlist wlan0 scan` fails ("Network is down"); the list stays empty and nothing is logged. With `wlan0` brought up by hand, the app's scan lists the simulated access point `KickEm-VM`. Connect: the app runs `killall -9 wpa_supplicant dhclient` (the system's `wpa_supplicant` dies), writes `/home/Kick/wpa_supplicant.conf` with `key_mgmt=WPA-PSK` and `psk=""` for the open network, and its own `wpa_supplicant` refuses it (`Line 4: Invalid passphrase length 0 (expected: 8..63)`); two `dhclient wlan0` remain, status "disconnected". On the unit the Wi-Fi start-up scripts may bring `wlan0` up first; open networks cannot be joined either way |
| Date and time | The Hardware tab's date and time steppers call `HwPref::setDate` / `setTime` | **The `fix/hwpref-date-time` bug is confirmed**, see below |
| CV, update | `/dev/i2c-3` absent, `swupdate` not installed | Reported unavailable by the hardware probe; not opened |
| Service | `systemctl stop ktkick.service` | `ktkick.service: Unit process 607 (KT_Kick) remains running after unit stopped`: the unit's `KillMode=process` signals only `kt_kick.sh`, so a stop or restart of the service leaves the app running on the unit too. `kickem stop` therefore also terminates `KT_Kick` |

Nothing crashed or hung in these runs.

### The date bug (`fix/hwpref-date-time`), observed

Preferences → Hardware, Date & Time. A touch on DAY, then one detent of the jog wheel (DAY 29 →
30), then a touch on HOUR and one detent (22 → 23). `kick.log`:

```
TestControl: echo DATE_TEST
...
sh: 1: 2026-10-30: not found
TestControl: echo TIME_TEST
sh: 1: 23:58:00: not found
```

`HwPref::setDate` (`Kick/modules/hardware/source/kick_Hwpref.cpp`, line 951 at `cc5fa831`) runs
`std::system( "2026-10-30" )` and `setTime` runs `std::system( "23:58:00" )`: the commands lack
`date -s`. The guest's clock is unchanged (`date` before and after: `Thu Oct 29 22:58:38 IST 2026`,
`22:58:53`), and the steppers snap back to the real date and time on the next refresh, so on the
unit the Hardware tab cannot set the clock. The failure is invisible in a release build: the
`DBG( "Failed to update date." )` is compiled out, and only the shell's message on stderr remains.
Also found while reading it: the month stepper is titled "Minute" (the screen shows MINUTE above
the month), and `date_YYYY.onValueChanged` calls `getDaysInMonth( Value, ... )` with the year as
the month.

## What is left on disk

`~/.cache/kickem/vm` after these runs, about 1 GB:

| File | Size | What |
|---|---|---|
| `debian-12-generic-arm64.qcow2` | 417 MB | The stock image, kept to re-provision without a download |
| `kick-debian12-provisioned.qcow2` | 544 MB | The provisioned image (standalone, compressed); every run boots an overlay of it |
| `edk2-aarch64-code.fd`, `edk2-licenses.txt` | 64 MB | QEMU's EDK2 firmware and its licences |
| `id_ed25519`, `.pub` | | The SSH key the image trusts |
| `SHA512SUMS`, `provision-serial.log`, `provision-times.json` | | Checksums, the last provisioning's serial log, its times |

A run's overlay (`<run>/disk.qcow2`, about 100-200 MB) is deleted when the VM stops. The artifact
is `~/.cache/kickem/build/<firmware>/<ref>/vm-artifact/` (33 MB). Docker keeps the image
`kickem-vm-builder:bookworm` (1.2 GB) and the volume `kickem-vm-build` (CPM cache, and one build
tree per firmware checkout, `build-<hash of its path>`, about 470 MB each) for incremental builds.
Until 2026-10-29 every checkout shared one tree: a freshly fetched ref whose files were older than
another ref's outputs was not recompiled, and its artifact held the other ref's binary under its
own manifest. Artifacts built before then by `--build` may be wrong; rebuild them. Remove them with
`docker image rm kickem-vm-builder:bookworm && docker volume rm kickem-vm-build`. To start over,
delete `~/.cache/kickem/vm`.
