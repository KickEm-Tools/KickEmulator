#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Faithful mode in one command: the same as `kickem run --mode vm`.
#
#   modes/vm/run.sh [--firmware URL|PATH] [--ref REF] [--artifact TARBALL] [--accel hvf|kvm|tcg]
#                   [--display] [--vm-serial pty|tcp] [--sound] [--disk IMG]... [--probe OUT.wav]
#                   [--detach] [--out DIR] [--no-build] [--no-start-delay]
#   modes/vm/run.sh provision [--force]     only download and provision the image
#
# It gets the fork's KT_Kick-linux-arm64 (--artifact, or built from the ref in a Debian 12 arm64
# container), provisions the stock Debian 12 arm64 cloud image once, boots a fresh overlay of it
# (QEMU virt, -accel hvf on Apple Silicon, 1024x600 virtio-gpu, usb-tablet, the guest UART on a pty
# or TCP socket, QMP, SSH on 127.0.0.1), deploys the artifact and the Starter Kit to the unit's
# paths with the fork's own ktkick.service / kt_kick.sh / Weston files, attaches the panel to the
# serial port, starts ktkick.service and waits for the handshake. See modes/vm/README.md.
HERE=$(cd "$(dirname "$0")" && pwd)
if [ "${1:-}" = provision ]; then
    shift
    exec python3 "$HERE/kickem_vm.py" provision "$@"
fi
exec "$HERE/../../kickem" run --mode vm "$@"
