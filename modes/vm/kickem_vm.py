#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""kickem_vm: faithful mode. The fork's built Linux arm64 KT_Kick in a QEMU arm64 VM (a stock
Debian 12 cloud image), with the panel on the VM's serial port.

    kickem_vm.py provision [--accel hvf|tcg|kvm]   download + provision the image once
    kickem_vm.py boot      --out RUN [--accel ..] [--display] [--serial pty|tcp] [--sound] [--disk IMG]...
    kickem_vm.py deploy    --out RUN --artifact DIR --firmware SRC [--probe GUEST.wav]
    kickem_vm.py start     --out RUN          systemctl start ktkick.service in the guest
    kickem_vm.py ssh       --out RUN [CMD...]
    kickem_vm.py tap       --out RUN X Y      touch (X,Y) of the 1024x600 screen (QMP input-send-event)
    kickem_vm.py shot      --out RUN OUT.png  QMP screendump of the VM display
    kickem_vm.py collect   --out RUN [--probe HOST.wav]   copy the probe WAV and logs back
    kickem_vm.py stop      --out RUN          stop the app, collect, power off the VM

`kickem run --mode vm` drives these (and starts the panel). Everything downloaded or built lives in
$KICKEM_CACHE/vm (default ~/.cache/kickem/vm), never in a repository: the Debian image, the EDK2
firmware copied from QEMU's share directory, the SSH key, the provisioned image, and per-run
overlays. Each run boots a fresh copy-on-write overlay of the provisioned image, so runs never
leak state into each other.

The guest sees the panel on its only PL011 UART, /dev/ttyAMA0; the app is started with
KICK_SERIAL_DEVICE=/dev/ttyAMA0 (no udev symlink to /dev/ttymxc0: the variable is the firmware's
documented way to name the port, and a symlink would hide which path was opened). The kernel
console is on the virtio console (hvc0), logged to <run>/boot.log, so nothing but the app talks
on ttyAMA0. KICK_HARDWARE is left unset, so the app's real Linux code runs.

Python 3.8+ standard library only.
"""

import argparse
import base64
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
IS_MAC = sys.platform == "darwin"

IMAGE_URL = "https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-generic-arm64.qcow2"
SUMS_URL = "https://cloud.debian.org/images/cloud/bookworm/latest/SHA512SUMS"
BASE = "debian-12-generic-arm64.qcow2"
PROVISIONED = "kick-debian12-provisioned.qcow2"
DISK_SIZE = "8G"
SCREEN_W, SCREEN_H = 1024, 600
MAC_MGMT, MAC_ETH0 = "52:54:00:4b:1c:01", "52:54:00:4b:1c:02"
GUEST_USER = "kickem"
SHARE = "/mnt/kickem"           # the run directory, shared over 9p
STAGE = "/var/tmp/kickem"       # deploy staging in the guest
GUEST_PROBE = "/var/tmp/kickem/probe.wav"
CANNOT_TELL = ("faithful mode cannot tell you latency, CPU headroom on the A53 cores, the codec, the GPU "
               "driver, or the control-surface microcontroller's own behaviour.")


def say(msg):
    print("kickem vm: " + msg, file=sys.stderr, flush=True)


def die(msg, code=1):
    say(msg)
    sys.exit(code)


def cache_dir():
    if os.environ.get("KICKEM_CACHE"):
        base = os.path.abspath(os.path.expanduser(os.environ["KICKEM_CACHE"]))
    else:
        base = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "kickem")
    d = os.path.join(base, "vm")
    os.makedirs(d, exist_ok=True)
    return d


def vm_path(name):
    return os.path.join(cache_dir(), name)


def qemu_bin():
    q = shutil.which("qemu-system-aarch64")
    if not q:
        die("qemu-system-aarch64 is not installed (macOS: brew install qemu; Debian/Ubuntu: apt install qemu-system-arm qemu-efi-aarch64)")
    return q


def qemu_share():
    q = os.path.realpath(qemu_bin())
    for c in (os.path.join(os.path.dirname(os.path.dirname(q)), "share", "qemu"),
              "/usr/share/qemu", "/usr/share/qemu-efi-aarch64", "/usr/share/AAVMF"):
        if os.path.isdir(c):
            yield c


def firmware_file():
    dst = vm_path("edk2-aarch64-code.fd")
    if not os.path.exists(dst):
        for d in qemu_share():
            for name in ("edk2-aarch64-code.fd", "QEMU_EFI.fd", "AAVMF_CODE.fd"):
                if os.path.exists(os.path.join(d, name)):
                    shutil.copy2(os.path.join(d, name), dst)
                    say("EDK2 firmware: copied %s into %s" % (os.path.join(d, name), dst))
                    return dst
        die("no EDK2 aarch64 firmware found in QEMU's share directory")
    return dst


def default_accel():
    if IS_MAC and os.uname().machine == "arm64":
        return "hvf"
    if sys.platform.startswith("linux") and os.uname().machine == "aarch64" and os.access("/dev/kvm", os.W_OK):
        return "kvm"
    return "tcg"


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def ssh_key():
    key = vm_path("id_ed25519")
    if not os.path.exists(key):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "kickem-vm", "-f", key], check=True)
    return key


# --- Image and provisioning --------------------------------------------------------------------
def ensure_base():
    path = vm_path(BASE)
    if os.path.exists(path):
        return path
    say("downloading %s" % IMAGE_URL)
    sums = urllib.request.urlopen(SUMS_URL, timeout=60).read().decode()
    want = next((l.split()[0] for l in sums.splitlines() if l.strip().endswith(" " + BASE)), None)
    if not want:
        die("no checksum for %s in %s" % (BASE, SUMS_URL))
    tmp = path + ".part"
    h = hashlib.sha512()
    with urllib.request.urlopen(IMAGE_URL, timeout=60) as r, open(tmp, "wb") as f:
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            h.update(b)
            f.write(b)
    if h.hexdigest() != want:
        os.remove(tmp)
        die("checksum mismatch for %s" % BASE)
    os.rename(tmp, path)
    say("verified SHA-512 of %s" % BASE)
    return path


def make_seed(seed_dir, iso):
    """A NoCloud seed (volume label cidata) from user-data and meta-data."""
    if os.path.exists(iso):
        os.remove(iso)
    if IS_MAC:
        subprocess.run(["hdiutil", "makehybrid", "-quiet", "-iso", "-joliet", "-default-volume-name", "cidata",
                        "-o", iso, seed_dir], check=True)
        return
    for tool in ("cloud-localds",):
        if shutil.which(tool):
            subprocess.run([tool, iso, os.path.join(seed_dir, "user-data"), os.path.join(seed_dir, "meta-data")], check=True)
            return
    for tool in ("genisoimage", "mkisofs", "xorrisofs"):
        if shutil.which(tool):
            subprocess.run([tool, "-quiet", "-output", iso, "-volid", "cidata", "-joliet", "-rock",
                            os.path.join(seed_dir, "user-data"), os.path.join(seed_dir, "meta-data")], check=True)
            return
    die("need hdiutil (macOS), cloud-localds, genisoimage, mkisofs or xorrisofs to make the cloud-init seed")


def qemu_common(accel, disk, memory="2048", cpus="4"):
    cpu = "host" if accel in ("hvf", "kvm") else "cortex-a53"   # the unit's core, for TCG
    return [qemu_bin(), "-machine", "virt,acpi=off" + (",highmem=on" if accel != "tcg" else ""),
            "-accel", accel + (",thread=multi" if accel == "tcg" else ""), "-cpu", cpu, "-smp", cpus, "-m", memory,
            "-bios", firmware_file(),
            "-drive", "if=none,id=root,format=qcow2,discard=unmap,file=" + disk,
            "-device", "virtio-blk-pci,drive=root,bootindex=0",
            "-nodefaults", "-no-user-config"]


def provision(a):
    out = vm_path(PROVISIONED)
    if os.path.exists(out) and not a.force:
        say("provisioned image exists: %s" % out)
        return out
    base = ensure_base()
    firmware_file()
    key = ssh_key()
    work = vm_path("provision")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    seed_dir = os.path.join(work, "seed")
    os.makedirs(seed_dir)
    pub = open(key + ".pub").read().strip()
    with open(os.path.join(HERE, "guest", "user-data")) as f:
        ud = f.read().replace("@PUBKEY@", pub)
    open(os.path.join(seed_dir, "user-data"), "w").write(ud)
    shutil.copy2(os.path.join(HERE, "guest", "meta-data"), os.path.join(seed_dir, "meta-data"))
    iso = os.path.join(work, "seed.iso")
    make_seed(seed_dir, iso)
    disk = os.path.join(work, "disk.qcow2")
    subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", "-F", "qcow2", "-b", base, disk, DISK_SIZE], check=True)
    log = os.path.join(work, "provision-serial.log")
    console = os.path.join(work, "provision-console.log")
    ssh_port = free_port()
    cmd = qemu_common(a.accel, disk) + [
        "-drive", "if=none,id=seed,format=raw,readonly=on,file=" + iso, "-device", "virtio-blk-pci,drive=seed",
        "-netdev", "user,id=mgmt,hostfwd=tcp:127.0.0.1:%d-:22" % ssh_port,
        "-device", "virtio-net-pci,netdev=mgmt,mac=" + MAC_MGMT,
        "-serial", "file:" + log,
        "-device", "virtio-serial-pci", "-chardev", "file,id=con,path=" + console, "-device", "virtconsole,chardev=con",
        "-display", "none"]
    say("provisioning (cloud-init: packages, console on hvc0, NIC names, modules); log %s; ssh -p %d" % (log, ssh_port))
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=open(os.path.join(work, "qemu.log"), "w"), stderr=subprocess.STDOUT)
    try:
        p.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        die("provisioning did not power off within %d s; see %s" % (a.timeout, log))
    took = time.monotonic() - t0
    text = open(log, errors="replace").read() + open(console, errors="replace").read()
    if "KICKEM_PROVISIONED" not in text:
        die("provisioning powered off without finishing (exit %s); see %s and %s" % (p.returncode, log, console))
    # A standalone image (the base folded in), compressed: the cache keeps one file per state.
    subprocess.run(["qemu-img", "convert", "-q", "-O", "qcow2", "-c", disk, out + ".part"], check=True)
    os.rename(out + ".part", out)
    with open(vm_path("provision-times.json"), "a") as f:
        f.write(json.dumps({"accel": a.accel, "provision_s": round(took, 1), "at": time.time()}) + "\n")
    shutil.copy2(log, vm_path("provision-serial.log"))
    shutil.rmtree(work, ignore_errors=True)
    say("provisioned in %.0f s: %s (%s)" % (took, out, human(os.path.getsize(out))))
    return out


def human(n):
    for u in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return "%.0f %s" % (n, u)
        n /= 1024.0
    return "%.1f TB" % n


# --- QMP ---------------------------------------------------------------------------------------
class QMP:
    def __init__(self, port, timeout=10):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        self.f = self.s.makefile("rwb")
        json.loads(self.f.readline())  # greeting
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        msg = {"execute": name}
        if args:
            msg["arguments"] = args
        self.f.write((json.dumps(msg) + "\n").encode())
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r:
                return r["return"]
            if "error" in r:
                raise RuntimeError("QMP %s: %s" % (name, r["error"].get("desc")))

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def qmp(st):
    return QMP(st["qmp_port"])


def tap(st, x, y, hold=0.08):
    """A touch at screen pixel (x, y): absolute pointer events on the usb-tablet, then press/release."""
    ax = lambda v, size: int(round(max(0, min(size - 1, v)) * 32767.0 / (size - 1)))
    q = qmp(st)
    try:
        move = [{"type": "abs", "data": {"axis": "x", "value": ax(x, SCREEN_W)}},
                {"type": "abs", "data": {"axis": "y", "value": ax(y, SCREEN_H)}}]
        q.cmd("input-send-event", events=move)
        time.sleep(0.03)
        q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
        time.sleep(hold)
        q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])
    finally:
        q.close()


def screendump(st, out):
    out = os.path.abspath(out)
    q = qmp(st)
    try:
        q.cmd("screendump", filename=out, format="png")
    finally:
        q.close()
    for _ in range(50):
        if os.path.exists(out) and os.path.getsize(out) > 0:
            break
        time.sleep(0.1)
    return out


# --- SSH -----------------------------------------------------------------------------------------
def ssh_base(st):
    return ["ssh", "-i", st["ssh_key"], "-p", str(st["ssh_port"]), "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=5",
            "-o", "BatchMode=yes", GUEST_USER + "@127.0.0.1"]


def ssh(st, command, check=True, capture=False, timeout=None):
    r = subprocess.run(ssh_base(st) + [command], stdin=subprocess.DEVNULL, timeout=timeout,
                       stdout=subprocess.PIPE if capture else None, stderr=subprocess.STDOUT if capture else None,
                       text=True)
    if check and r.returncode != 0:
        if capture:
            sys.stderr.write(r.stdout or "")
        die("guest command failed (exit %d): %s" % (r.returncode, command))
    return r.stdout if capture else r.returncode


def scp_to(st, srcs, dst):
    subprocess.run(["scp", "-q", "-r", "-i", st["ssh_key"], "-P", str(st["ssh_port"]), "-o", "StrictHostKeyChecking=no",
                    "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR"] + list(srcs)
                   + ["%s@127.0.0.1:%s" % (GUEST_USER, dst)], check=True)


def scp_from(st, src, dst, check=False):
    return subprocess.run(["scp", "-q", "-r", "-i", st["ssh_key"], "-P", str(st["ssh_port"]), "-o", "StrictHostKeyChecking=no",
                           "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR",
                           "%s@127.0.0.1:%s" % (GUEST_USER, src), dst], check=check).returncode


# --- State ---------------------------------------------------------------------------------------
def state_path(d):
    return os.path.join(d, "state.json")


def read_state(d):
    try:
        with open(state_path(d)) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_state(d, st):
    tmp = state_path(d) + ".tmp"
    with open(tmp, "w") as f:
        json.dump(st, f, indent=2)
    os.replace(tmp, state_path(d))


def update_state(d, **kv):
    st = read_state(d) or {}
    st.update(kv)
    write_state(d, st)
    return st


def alive(pid):
    if not pid:
        return False
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


# --- Boot ----------------------------------------------------------------------------------------
def boot(d, accel=None, display=False, serial="pty", sound=False, disks=(), ssh_timeout=None, vnc=None):
    """Boots a fresh overlay of the provisioned image; returns the state (pids, ports, pty).
    vnc: a unix socket path for QEMU's VNC server on the VM's display (the panel's embedded
    screen, kickem run --screen embedded); its pointer is absolute on the usb-tablet."""
    accel = accel or default_accel()
    image = vm_path(PROVISIONED)
    if not os.path.exists(image):
        die("no provisioned image; run: %s provision" % os.path.join(HERE, "kickem_vm.py"))
    os.makedirs(d, exist_ok=True)
    disk = os.path.join(d, "disk.qcow2")
    if os.path.exists(disk):
        os.remove(disk)
    subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", "-F", "qcow2", "-b", image, disk], check=True)
    ssh_port, qmp_port = free_port(), free_port()
    boot_log = os.path.join(d, "boot.log")
    cmd = qemu_common(accel, disk) + [
        "-name", "kickem-vm",
        # The display: 1024x600, as the unit's panel. virtio-gpu gives Weston a DRM device.
        "-device", "virtio-gpu-pci,xres=%d,yres=%d" % (SCREEN_W, SCREEN_H),
        # Touch: an absolute pointer (QMP input-send-event abs axes). A keyboard for -display use.
        "-device", "qemu-xhci", "-device", "usb-tablet", "-device", "usb-kbd",
        # eth0 (the device's Ethernet port: the app configures it) on its own isolated user network;
        # mgmt0 for SSH, forwarded to 127.0.0.1 only.
        "-netdev", "user,id=eth0,restrict=on,net=192.168.76.0/24", "-device", "virtio-net-pci,netdev=eth0,mac=" + MAC_ETH0 + ",addr=0x8",
        "-netdev", "user,id=mgmt,hostfwd=tcp:127.0.0.1:%d-:22" % ssh_port, "-device", "virtio-net-pci,netdev=mgmt,mac=" + MAC_MGMT + ",addr=0x9",
        # The run directory, shared as /mnt/kickem: the app log, the test-control file.
        "-fsdev", "local,id=share,path=%s,security_model=none" % d, "-device", "virtio-9p-pci,fsdev=share,mount_tag=kickem",
        # Kernel console and systemd on hvc0 -> boot.log.
        "-device", "virtio-serial-pci", "-chardev", "file,id=con,path=" + boot_log, "-device", "virtconsole,chardev=con",
        "-qmp", "tcp:127.0.0.1:%d,server=on,wait=off" % qmp_port,
        "-display", ("cocoa" if IS_MAC else "gtk") if display else "none"]
    if vnc:
        # The display for the panel (--screen embedded): RFB on a unix socket, never on a network.
        # QEMU sends the viewer's pointer to the usb-tablet as absolute coordinates.
        cmd += ["-vnc", "unix:" + vnc]
    # The guest's PL011 UART (/dev/ttyAMA0): the panel's end of the wire.
    if serial == "tcp":
        serial_port = free_port()
        cmd += ["-chardev", "socket,id=panel,host=127.0.0.1,port=%d,server=on,wait=off" % serial_port, "-serial", "chardev:panel"]
    else:
        serial_port = None
        cmd += ["-chardev", "pty,id=panel", "-serial", "chardev:panel"]
    if sound:
        # The guest's second card: the virtio sound card to this computer's default output.
        cmd += ["-audiodev", ("coreaudio" if IS_MAC else "pa") + ",id=snd", "-device", "virtio-sound-pci,audiodev=snd"]
    for i, img in enumerate(disks):
        # Stand-ins for the SD card and the USB stick: USB mass storage, as on the unit.
        cmd += ["-drive", "if=none,id=xd%d,format=%s,file=%s" % (i, "qcow2" if img.endswith(".qcow2") else "raw", os.path.abspath(img)),
                "-device", "usb-storage,drive=xd%d" % i]
    open(boot_log, "w").close()
    qlog = open(os.path.join(d, "qemu.log"), "w")
    qlog.write("$ %s\n" % " ".join(shlex.quote(c) for c in cmd))
    qlog.flush()
    say("booting (%s): %s" % (accel, " ".join(shlex.quote(c) for c in cmd)))
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=qlog, stderr=subprocess.STDOUT, start_new_session=True)
    st = {"mode": "vm", "accel": accel, "qemu_pid": p.pid, "ssh_port": ssh_port, "qmp_port": qmp_port,
          "ssh_key": ssh_key(), "disk": disk, "boot_log": boot_log, "serial": serial, "serial_port": serial_port, "vnc": vnc,
          "run_dir": d, "booted": time.time()}
    update_state(d, **st)
    # The pty's name, from QMP.
    q = None
    for _ in range(100):
        if not alive(p.pid) or p.poll() is not None:
            die("QEMU exited at start; see %s" % os.path.join(d, "qemu.log"))
        try:
            q = QMP(qmp_port)
            break
        except OSError:
            time.sleep(0.1)
    if q is None:
        die("no QMP on port %d" % qmp_port)
    if serial == "tcp":
        st["serial_device"] = "tcp:127.0.0.1:%d" % serial_port
    else:
        for c in q.cmd("query-chardev"):
            if c["label"] == "panel":
                st["serial_device"] = c["filename"].split(":", 1)[1]
    q.close()
    update_state(d, **st)
    say("guest UART /dev/ttyAMA0 is %s on this computer" % st["serial_device"])
    # SSH up = the guest has booted (multi-user).
    limit = ssh_timeout or (180 if accel != "tcg" else 1800)
    end = time.monotonic() + limit
    while time.monotonic() < end:
        if p.poll() is not None:
            die("QEMU exited during boot; see %s and %s" % (boot_log, os.path.join(d, "qemu.log")))
        r = subprocess.run(ssh_base(st) + ["true"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            break
        time.sleep(1)
    else:
        die("no SSH after %d s; see %s" % (limit, boot_log))
    st["boot_s"] = round(time.monotonic() - t0, 1)
    update_state(d, **st)
    say("booted: SSH up %.1f s after QEMU started" % st["boot_s"])
    return read_state(d)


# --- Deploy ----------------------------------------------------------------------------------------
OVERLAY = "Linux/buildroot/board/freescale/imx8mm_var_som/rootfs_overlay"
OVERLAY_FILES = [  # (in the fork's rootfs_overlay, installed as)
    ("etc/systemd/system/ktkick.service", "/etc/systemd/system/ktkick.service"),
    ("usr/bin/kt_kick.sh", "/usr/bin/kt_kick.sh"),
    ("usr/lib/systemd/system/weston@.service", "/usr/lib/systemd/system/weston@.service"),
    ("etc/xdg/weston/weston.ini", "/etc/xdg/weston/weston.ini"),
    ("etc/udev/rules.d/71-weston-drm.rules", "/etc/udev/rules.d/71-weston-drm.rules"),
    ("etc/asound.conf", "/etc/asound.conf"),
    ("etc/profile.d/weston.sh", "/etc/profile.d/weston.sh"),
]


def find_artifact(path):
    """A KT_Kick-linux-arm64 directory or tarball (from artifact.sh); returns the tarball to send."""
    path = os.path.abspath(path)
    if os.path.isfile(path) and path.endswith(".tar.gz"):
        return path
    for c in (path + ".tar.gz", os.path.join(path, "KT_Kick-linux-arm64.tar.gz"),
              os.path.join(os.path.dirname(path), "KT_Kick-linux-arm64.tar.gz")):
        if os.path.isfile(c):
            return c
    die("no KT_Kick-linux-arm64.tar.gz at %s (make one with modes/vm/artifact.sh)" % path)


def deploy(d, artifact, firmware, env, start_delay=True):
    st = read_state(d)
    t0 = time.monotonic()
    tarball = find_artifact(artifact)
    stage = os.path.join(d, "stage")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "overlay"))
    missing = []
    for src, dst in OVERLAY_FILES:
        p = os.path.join(firmware, OVERLAY, src)
        if os.path.exists(p):
            os.makedirs(os.path.dirname(os.path.join(stage, "overlay", dst.lstrip("/"))), exist_ok=True)
            shutil.copy2(p, os.path.join(stage, "overlay", dst.lstrip("/")))
        else:
            missing.append(src)
    if missing:
        say("the fork's rootfs_overlay lacks %s; the VM's defaults are used for those" % ", ".join(missing))
    kit = os.path.join(ROOT, "content", "StarterKit")
    shutil.copytree(kit, os.path.join(stage, "StarterKit"), ignore=shutil.ignore_patterns("make_starter_kit.py", ".DS_Store", "__pycache__"))
    with open(os.path.join(stage, "kickem.env"), "w") as f:
        for k, v in env.items():
            f.write("%s=%s\n" % (k, v))
    shutil.copy2(os.path.join(HERE, "guest", "deploy.sh"), os.path.join(stage, "deploy.sh"))
    shutil.copy2(tarball, os.path.join(stage, "KT_Kick-linux-arm64.tar.gz"))
    ssh(st, "sudo rm -rf %s && sudo mkdir -p %s && sudo chown %s %s" % (STAGE, STAGE, GUEST_USER, STAGE))
    scp_to(st, [os.path.join(stage, x) for x in os.listdir(stage)], STAGE)
    shutil.rmtree(stage, ignore_errors=True)
    say("deploying %s and the Starter Kit into the device paths" % os.path.basename(tarball))
    rc = ssh(st, "sudo sh %s/deploy.sh %s %s" % (STAGE, STAGE, "delay" if start_delay else "nodelay"), check=False)
    if rc != 0:
        die("deploy failed in the guest (exit %d)" % rc)
    took = round(time.monotonic() - t0, 1)
    update_state(d, deploy_s=took, artifact=tarball)
    say("deployed in %.1f s" % took)


def start_app(d):
    st = read_state(d)
    ssh(st, "sudo systemctl start ktkick.service")
    update_state(d, app_started=time.time())


def guest_app_pid(st):
    out = ssh(st, "pidof KT_Kick || true", check=False, capture=True, timeout=15) or ""
    return int(out.split()[0]) if out.split() and out.split()[0].isdigit() else 0


def collect(d, probe=None):
    """Copies back what the guest wrote: the probe WAV, the app's own logs, the journal."""
    st = read_state(d)
    if not st or not alive(st.get("qemu_pid", 0)):
        return
    gl = os.path.join(d, "guest")
    os.makedirs(gl, exist_ok=True)
    ssh(st, "sudo sh -c 'journalctl -b --no-pager > %s/journal.txt 2>&1; journalctl -b -u ktkick -u weston@root --no-pager > %s/services.txt 2>&1;"
            " cp -r /home/Kick/logs %s/home-Kick-logs 2>/dev/null; ls -laR /home/Kick /app/demos /data/sounds > %s/device-paths.txt 2>&1;"
            " chown -R %s %s'" % (STAGE, STAGE, STAGE, STAGE, GUEST_USER, STAGE), check=False, timeout=60)
    for name in ("journal.txt", "services.txt", "home-Kick-logs", "device-paths.txt"):
        scp_from(st, "%s/%s" % (STAGE, name), gl)
    if probe:
        if scp_from(st, GUEST_PROBE, probe) == 0:
            say("probe WAV copied back to %s" % probe)
        else:
            say("no probe WAV in the guest (%s)" % GUEST_PROBE)


def stop(d, probe=None, quiet=False):
    st = read_state(d)
    if not st or st.get("mode") != "vm":
        if not quiet:
            say("nothing to stop in %s" % d)
        return 0
    pid = st.get("qemu_pid", 0)
    if alive(pid):
        # The unit's ktkick.service has KillMode=process: stopping it signals only kt_kick.sh, and
        # KT_Kick keeps running ("Unit process ... remains running after unit stopped"). Terminate
        # the app itself too, so it closes the probe WAV.
        out = ssh(st, "sudo systemctl stop ktkick.service; sudo pkill -TERM -x KT_Kick;"
                " for i in $(seq 50); do pidof KT_Kick >/dev/null || break; sleep 0.2; done;"
                " if pidof KT_Kick >/dev/null; then sudo pkill -KILL -x KT_Kick; echo 'KT_Kick did not quit on SIGTERM; killed';"
                " else echo 'KT_Kick quit'; fi", check=False, capture=True, timeout=30)
        say("in the guest: %s" % (out or "?").strip())
        collect(d, probe or st.get("probe_host"))
        ssh(st, "sudo systemctl poweroff", check=False, timeout=15)
        end = time.monotonic() + 30
        while alive(pid) and time.monotonic() < end:
            try:
                os.waitpid(pid, os.WNOHANG)
            except (ChildProcessError, OSError):
                pass
            time.sleep(0.2)
        if alive(pid):
            say("QEMU (pid %d) still running; quitting it over QMP" % pid)
            try:
                q = qmp(st)
                q.cmd("quit")
                q.close()
            except (OSError, RuntimeError):
                os.kill(pid, signal.SIGKILL)
    if os.path.exists(st.get("disk", "")):
        os.remove(st["disk"])  # the run's overlay; the provisioned image stays
    update_state(d, stopped=time.time())
    if not quiet:
        say("VM stopped")
    return 0


# --- CLI ----------------------------------------------------------------------------------------
def main(argv=None):
    p = argparse.ArgumentParser(prog="kickem_vm.py", description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    pv = sub.add_parser("provision")
    pv.add_argument("--accel", default=default_accel())
    pv.add_argument("--force", action="store_true")
    pv.add_argument("--timeout", type=int, default=1800)
    b = sub.add_parser("boot")
    b.add_argument("--out", required=True)
    b.add_argument("--accel", default=None)
    b.add_argument("--display", action="store_true")
    b.add_argument("--serial", choices=("pty", "tcp"), default="pty")
    b.add_argument("--sound", action="store_true")
    b.add_argument("--disk", action="append", default=[])
    dp = sub.add_parser("deploy")
    dp.add_argument("--out", required=True)
    dp.add_argument("--artifact", required=True)
    dp.add_argument("--firmware", required=True)
    dp.add_argument("--probe", default=GUEST_PROBE)
    dp.add_argument("--no-start-delay", action="store_true")
    for name in ("start", "ssh", "tap", "shot", "collect", "stop"):
        s = sub.add_parser(name)
        s.add_argument("--out", required=True)
        if name in ("collect", "stop"):
            s.add_argument("--probe")
        s.add_argument("rest", nargs="*")
    a = p.parse_args(argv)
    d = os.path.abspath(a.out) if getattr(a, "out", None) else None
    if a.cmd == "provision":
        provision(a)
    elif a.cmd == "boot":
        boot(d, a.accel, a.display, a.serial, a.sound, a.disk)
    elif a.cmd == "deploy":
        deploy(d, a.artifact, os.path.abspath(a.firmware), app_env(a.probe), not a.no_start_delay)
    elif a.cmd == "start":
        start_app(d)
    elif a.cmd == "ssh":
        return subprocess.call(ssh_base(read_state(d)) + a.rest)
    elif a.cmd == "tap":
        tap(read_state(d), float(a.rest[0]), float(a.rest[1]))
    elif a.cmd == "shot":
        print(screendump(read_state(d), a.rest[0]))
    elif a.cmd == "collect":
        collect(d, a.probe)
    elif a.cmd == "stop":
        return stop(d, a.probe)
    return 0


def app_env(probe=GUEST_PROBE, extra=None):
    """The app's environment in the guest (ktkick.service drop-in). KICK_HARDWARE stays unset."""
    env = {"DISPLAY": ":0", "XDG_RUNTIME_DIR": "/run/user/0",
           "KICK_SERIAL_DEVICE": "/dev/ttyAMA0",
           "KICK_AUDIO_INPUT": os.environ.get("KICK_AUDIO_INPUT", "silence"),
           "KICK_TEST_CONTROL": SHARE + "/test.ctl"}
    if probe:
        env["KICK_AUDIO_PROBE"] = probe
    if os.environ.get("KICK_LOG_NOTES"):
        env["KICK_LOG_NOTES"] = os.environ["KICK_LOG_NOTES"]
    env.update(extra or {})
    return env


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
