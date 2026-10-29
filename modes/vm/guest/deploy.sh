#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs as root in the faithful-mode guest (kickem_vm.py deploy): installs the fork's built
# KT_Kick-linux-arm64 and the Starter Kit at the unit's paths, the fork's own service files from
# its rootfs_overlay, and a ktkick.service drop-in with the emulator's environment. Does not start
# the app: the host attaches the panel first, then runs `systemctl start ktkick.service`.
#
#   deploy.sh STAGE delay|nodelay
#
# STAGE holds KT_Kick-linux-arm64.tar.gz, StarterKit/, overlay/ (paths as installed), kickem.env.
# nodelay starts /usr/bin/KT_Kick directly instead of the device's kt_kick.sh, which sleeps 15 s.
set -eu
STAGE=$1
DELAY=${2:-delay}
cd "$STAGE"
say() { echo "deploy: $*"; }

# The run directory of the host, shared over 9p: the app log and the test-control file.
mountpoint -q /mnt/kickem || mount -t 9p -o trans=virtio,version=9p2000.L,msize=524288,cache=none kickem /mnt/kickem
say "host run directory on /mnt/kickem ($(ls /mnt/kickem | wc -l) entries)"

# The artifact (layout of the fork's .github/workflows/build.yml): /usr/bin/KT_Kick, /app/*.
rm -rf pkg && mkdir pkg && tar -C pkg -xzf KT_Kick-linux-arm64.tar.gz
P=pkg/KT_Kick-linux-arm64
install -m 0755 "$P/KT_Kick" /usr/bin/KT_Kick
rm -rf /app/assets /app/templates /app/demos
cp -a "$P/assets" "$P/templates" "$P/demos" /app/
[ -f "$P/manifest.json" ] && cp "$P/manifest.json" /app/kickem-manifest.json
say "KT_Kick $(sed -n 's/.*"git_sha": "\([0-9a-f]*\)".*/\1/p' "$P/manifest.json" 2>/dev/null | cut -c1-12) installed: $(stat -c %s /usr/bin/KT_Kick) bytes"
if ldd /usr/bin/KT_Kick | grep -q "not found"; then
    ldd /usr/bin/KT_Kick | grep "not found" | sed 's/^/deploy: MISSING /'
    exit 3
fi
# Start from a clean unit state: no projects, prefs or logs from the image.
rm -rf /home/Kick/projects/* /home/Kick/prefs/* /home/Kick/logs/*
mkdir -p /home/Kick/projects /home/Kick/logs /home/Kick/prefs /data/sounds

# The Starter Kit (content/StarterKit/README.md, with the Linux paths of kick_paths.cpp).
mkdir -p /app/assets/StarterKit /data/sounds/StarterKit "/app/demos/Starter Kit" /app/assets/Default/recent_project
cp -a StarterKit/samples/. /app/assets/StarterKit/
cp -a StarterKit/samples/. /data/sounds/StarterKit/
cp -a "StarterKit/demos/Starter Kit/." "/app/demos/Starter Kit/"
cp -a "StarterKit/recent_project/Starter Kit.png" /app/assets/Default/recent_project/
say "Starter Kit in /app/assets/StarterKit, /data/sounds/StarterKit, /app/demos/Starter Kit"

# The fork's service files, as on the unit.
if [ -d overlay ]; then
    (cd overlay && find . -type f) | while read -r f; do
        mkdir -p "$(dirname "/${f#./}")"
        cp "overlay/$f" "/${f#./}"
        say "from the fork's rootfs_overlay: /${f#./}"
    done
    [ -f /usr/bin/kt_kick.sh ] && chmod 0755 /usr/bin/kt_kick.sh
fi
# weston.ini on the unit enables NXP's G2D (use-g2d=1), which the VM does not have; Weston ignores
# unknown keys, so it stays as it is.

# ktkick.service as on the unit, plus the emulator's environment and log redirection. The unit's
# service has no DISPLAY; there it comes from the Weston session environment.
mkdir -p /etc/systemd/system/ktkick.service.d
{
    echo "# Written by KickEmulator modes/vm/guest/deploy.sh: the emulator's additions only."
    echo "[Unit]"
    echo "After=weston@root.service"
    echo "Wants=weston@root.service"
    echo "[Service]"
    while IFS= read -r line; do [ -n "$line" ] && echo "Environment=\"$line\""; done < kickem.env
    echo "StandardOutput=append:/mnt/kickem/kick.log"
    echo "StandardError=inherit"
    echo "Restart=no"
    if [ "$DELAY" = nodelay ]; then
        echo "ExecStart="
        echo "ExecStart=/usr/bin/KT_Kick"
    fi
} > /etc/systemd/system/ktkick.service.d/kickem.conf
systemctl daemon-reload
systemctl disable ktkick.service >/dev/null 2>&1 || true
say "ktkick.service drop-in:"
sed 's/^/deploy:   /' /etc/systemd/system/ktkick.service.d/kickem.conf

# The session: the unit starts weston@root from a udev rule when the DRM card appears (which
# happened at boot, before the rule was installed); start it now. Weston's Xwayland (weston.ini
# xwayland=true) listens on :0, which the app uses (DISPLAY=:0, as JUCE talks X11).
udevadm control --reload || true
systemctl start weston@root.service
i=0
until [ -S /tmp/.X11-unix/X0 ] || [ $i -ge 100 ]; do i=$((i + 1)); sleep 0.1; done
if [ -S /tmp/.X11-unix/X0 ]; then
    say "weston@root running (pid $(systemctl show -p MainPID --value weston@root.service)), Xwayland on :0, $(grep -m1 'preferred, current' /run/user/0/weston.log | sed 's/^ *//')"
else
    say "weston did not start"; journalctl -u weston@root --no-pager | tail -20; tail -20 /run/user/0/weston.log; exit 4
fi

# Audio: card 0 is the loopback (snd-aloop), which the unit's asound.conf plays to as card0.
aplay -l 2>/dev/null | sed -n 's/^card/deploy: ALSA card/p'
mkdir -p "$(dirname "$(sed -n 's/^KICK_AUDIO_PROBE=//p' kickem.env)")" 2>/dev/null || true
rm -f /var/tmp/kickem/probe.wav
exit 0
