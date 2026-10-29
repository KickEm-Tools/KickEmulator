#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The job summary of .github/workflows/test-firmware.yml, as Markdown on stdout.

    python .github/ci/summary.py EVIDENCE

Reads EVIDENCE/results.tsv (from run-checks.sh), each check's output.txt and run/state.json, and
the environment the workflow sets (MODE, FW_REPO, FW_REF, FW_COMMIT, ...).
"""
import glob
import json
import os
import platform
import sys
import time

CANNOT_TELL = {
    "fast": "Fast mode runs the fork's **desktop** `Kick` app, not the device's Linux build. It cannot "
            "tell you anything Linux-specific (systemd units, scripts, device nodes, the Wi-Fi / Ethernet / "
            "CV / firmware-update screens), and timing, latency and CPU load are the runner's, not the "
            "unit's A53 cores. The panel plays the control-surface microcontroller's side of the protocol, "
            "not its firmware.",
    "vm": "Faithful mode runs the fork's Linux arm64 `KT_Kick` in a QEMU VM (stock Debian 12, the fork's "
          "own `ktkick.service` and scripts). It cannot tell you latency, CPU headroom on the A53 cores, "
          "the codec, the GPU driver, or the control-surface microcontroller's own behaviour.",
}


def env(name, default=""):
    return os.environ.get(name) or default


def fmt_s(s):
    try:
        s = float(s)
    except (TypeError, ValueError):
        return ""
    return "%d min %02d s" % divmod(int(round(s)), 60) if s >= 60 else "%.0f s" % s


def ci_limits(mode):
    os_name, arch = env("RUNNER_OS"), env("RUNNER_ARCH")
    lines = []
    if mode == "fast" and os_name == "macOS":
        dev = ""
        try:
            text = open(os.path.join(sys.argv[1], "audio-devices.txt"), errors="replace").read()
            blocks = text.split("\n\n")
            for i, b in enumerate(blocks):
                if "Default Output Device: Yes" in b and i:
                    # system_profiler: the device's name, a blank line, then its properties.
                    dev = blocks[i - 1].strip().splitlines()[-1].strip().rstrip(":")
                    break
        except OSError:
            pass
        lines.append("Audio goes to the runner's virtual output device%s. The probe (`KICK_AUDIO_PROBE`) records "
                     "the engine's master output before it reaches any device: it says nothing about a codec or "
                     "a DAC." % (" (%s)" % dev if dev else ""))
        lines.append("The runner is a virtual machine (GitHub-hosted macOS arm64: 3 vCPUs of an M1, 7 GB): "
                     "timings say nothing about the unit.")
    elif mode == "fast" and os_name == "Linux":
        lines.append("Linux desktop: audio goes to a PulseAudio null sink through ALSA's pulse plugin (hosted "
                     "runners have no sound card); the screen is Xvfb. `KICK_HARDWARE=none`, so the Linux "
                     "hardware screens show as not available.")
    elif mode == "vm":
        kvm = os.path.exists("/dev/kvm")
        lines.append("QEMU runs under %s: hosted %s runners %s. The guest is many times slower than a unit; "
                     "its timings and any audio dropouts are the emulator's, not the device's."
                     % ("KVM" if kvm else "**TCG (emulation)**", arch or "arm64",
                        "expose /dev/kvm" if kvm else "have no KVM"))
        lines.append("Audio leaves through ALSA's loopback driver (`snd-aloop`) in the guest; the probe WAV is "
                     "copied back after the run.")
    return lines


def check_rows(ev):
    rows = []
    try:
        for line in open(os.path.join(ev, "results.tsv")):
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 4:
                rows.append(parts[:4])
    except OSError:
        pass
    return rows


def main():
    ev = sys.argv[1]
    mode = env("MODE", "fast")
    out = []
    rows = check_rows(ev)
    failed = [r for r in rows if r[1] not in ("pass", "skip")]
    verdict = "no checks ran" if not rows else ("all passed" if not failed else "%d of %d failed" % (len(failed), len(rows)))
    icon = {"pass": "PASS", "fail": "FAIL", "skip": "SKIP", "unknown": "UNKNOWN"}
    out.append("## KickEmulator, %s mode: %s" % (mode, verdict))
    out.append("")
    commit = env("FW_COMMIT")
    server = env("GITHUB_SERVER_URL", "https://github.com")
    fw = env("FW_REPO")
    out.append("| | |")
    out.append("|---|---|")
    out.append("| Firmware | [%s](%s/%s) `%s` @ [`%s`](%s/%s/commit/%s) %s |" % (
        fw, server, fw, env("FW_REF"), commit[:12], server, fw, commit, env("FW_SUBJECT").replace("|", "/")))
    emu = env("EMU_REPO")
    out.append("| Emulator | [%s](%s/%s) `%s` @ `%s` |" % (emu, server, emu, env("EMU_REF"), env("EMU_COMMIT")[:12]))
    out.append("| Runner | `%s` (%s %s, image %s %s, %d CPUs) |" % (
        env("RUNNER_LABEL"), env("RUNNER_OS"), env("RUNNER_ARCH"), env("ImageOS"), env("ImageVersion"), os.cpu_count() or 0))
    times = []
    if env("BUILD_S"):
        times.append("build (Kick + panel) %s" % fmt_s(env("BUILD_S")))
    if env("ARTIFACT_S"):
        times.append("KT_Kick-linux-arm64 artifact %s" % fmt_s(env("ARTIFACT_S")))
    if env("PROVISION_S"):
        times.append("VM image %s (%s)" % (fmt_s(env("PROVISION_S")), "from the cache" if env("PROVISION_CACHED") == "true" else "provisioned"))
    if env("T_START"):
        times.append("job so far %s" % fmt_s(time.time() - float(env("T_START"))))
    if times:
        out.append("| Times | %s |" % "; ".join(times))
    ccache = os.path.join(ev, "ccache.txt")
    if os.path.exists(ccache):
        hits = [l.strip() for l in open(ccache, errors="replace") if l.strip().lower().startswith(("hits", "misses", "cacheable"))]
        if hits:
            out.append("| ccache | %s |" % "; ".join(hits[:3]))
    out.append("")

    if rows:
        out.append("| Check | Result | Time | Notes |")
        out.append("|---|---|---|---|")
        for name, status, rc, secs in rows:
            notes = []
            path = os.path.join(ev, name, "output.txt")
            if os.path.exists(path):
                lines = open(path, errors="replace").read().splitlines()
                npass = sum(1 for l in lines if l.startswith("PASS"))
                nfail = sum(1 for l in lines if l.startswith("FAIL"))
                notes.append("%d PASS, %d FAIL" % (npass, nfail))
                notes += [l[6:].strip() for l in lines if l.startswith("FAIL")][:5]
                notes += [l.strip() for l in lines if l.startswith(("NOTE", "INFO"))][:2]
                notes += [l.strip() for l in lines if l.startswith(("TIME", "      that is", "      kickem run to LIVE"))][:2]
            st_path = os.path.join(ev, name, "run", "state.json")
            if os.path.exists(st_path):
                try:
                    st = json.load(open(st_path))
                    if mode == "vm" and st.get("boot_s"):
                        notes.append("VM boot %s, deploy %s, ktkick.service to LIVE %s (accel %s)" % (
                            fmt_s(st.get("boot_s")), fmt_s(st.get("deploy_s")), fmt_s(st.get("live_s")), st.get("accel")))
                    elif st.get("live_s") is not None:
                        notes.append("handshake LIVE %s after start" % fmt_s(st.get("live_s")))
                except ValueError:
                    pass
            out.append("| `%s` | %s | %s | %s |" % (name, icon.get(status, status), fmt_s(secs),
                                                   "<br>".join(n.replace("|", "/") for n in notes)))
        out.append("")

    out.append("### What this run cannot tell you")
    out.append("")
    for name, status, rc, secs in rows:
        path = os.path.join(ev, name, "output.txt")
        if os.path.exists(path) and any(l.startswith("NOTE  audio not verified") for l in open(path, errors="replace")):
            out.append("- **`%s` did not verify the audio**: the guest rendered far slower than real time (see its NOTE line), "
                       "so the level, clipping and silence results are printed, not asserted. Boot, the unit's service, the "
                       "serial handshake, the project load, LEDs, pad colours and the screen were checked." % name)
    out.append("- " + CANNOT_TELL.get(mode, ""))
    for l in ci_limits(mode):
        out.append("- " + l)
    out.append("")
    files = sorted(os.path.relpath(p, ev) for p in glob.glob(os.path.join(ev, "**", "*"), recursive=True)
                   if os.path.isfile(p) and "/run/stage/" not in p and not p.endswith((".ctl", ".ctl.seq")))
    out.append("**Evidence:** artifact `%s` (%d files): app log `kick.log`, panel log `panel.log` (`SimSerial:` "
               "frames), probe WAV `probe.wav`, screenshots `*.png`, each check's `output.txt`, the build log." % (
                   env("ARTIFACT_NAME"), len(files)))
    print("\n".join(out))


if __name__ == "__main__":
    main()
