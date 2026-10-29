# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Shared by the checks: argument parsing, starting and stopping the emulator, the probe clock.
# Source it after setting NAME (the check's name, for the default OUTDIR):
#
#   NAME=transport; . "$(dirname "$0")/lib/common.sh"; parse_args "$@"
#
# Common arguments: [--mode fast] [--firmware URL|PATH] [--ref REF] [--no-build] [--no-fetch] [OUTDIR]
# Sets: CHECKS ROOT OUT RUN CTL KLOG PLOG PROBE MODE RUN_ARGS; after start_emulator also KPID.

CHECKS=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$CHECKS/.." && pwd)
CTL="$CHECKS/lib/kickem_ctl"
MODE=fast
RUN_ARGS=""

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --mode) MODE=$2; shift 2 ;;
            --firmware|--ref) RUN_ARGS="$RUN_ARGS $1 $2"; shift 2 ;;
            --no-build|--no-fetch) RUN_ARGS="$RUN_ARGS $1"; shift ;;
            -h|--help) sed -n '2,/^set -u/p' "$0" | sed '$d'; exit 0 ;;
            -*) echo "$NAME: unknown option $1" >&2; exit 2 ;;
            *) break ;;
        esac
    done
    OUT=${1:-$CHECKS/out/$NAME}
    mkdir -p "$OUT"
    OUT=$(cd "$OUT" && pwd)
    RUN="$OUT/run"
    KLOG="$RUN/kick.log"
    PLOG="$RUN/panel.log"
    PROBE="$OUT/probe.wav"
    export KICKEM_RUN_DIR="$RUN"
    case "$MODE" in
        fast) ;;
        vm) echo "SKIP  $NAME: --mode vm is not supported by this check yet"; exit 3 ;;
        *) echo "$NAME: --mode fast|vm" >&2; exit 2 ;;
    esac
}

banner() {
    echo "== $NAME, mode $MODE"
    echo "   cannot tell you: anything Linux-specific (systemd units, scripts, device nodes, the Wi-Fi /"
    echo "   Ethernet / CV / update screens); timing and CPU load are this computer's, not the unit's."
}

# start_emulator [kickem run options...]: a fresh run (demos, templates, prefs restored) with the
# probe at $PROBE, panel tracing on, under the machine-wide lock. Extra environment for the app
# (KICK_AUDIO_INPUT, KICK_LOG_NOTES) is taken from the caller's environment.
start_emulator() {
    rm -rf "$RUN"
    rm -f "$PROBE"
    PROBE_OFFSET=""
    # shellcheck disable=SC2086
    KICK_SIM_LOG_SURFACE=1 "$ROOT/kickem" run $RUN_ARGS --fresh --detach --probe "$PROBE" --out "$RUN" "$@" \
        > "$OUT/kickem.log" 2>&1
    STATUS=$?
    if [ "$STATUS" -ne 0 ]; then
        tail -15 "$OUT/kickem.log" | sed 's/^/      /'
        echo "FAIL  kickem run (exit $STATUS), see $OUT/kickem.log"
        exit 1
    fi
    KPID=$(state kick_pid)
    echo "   firmware $(state firmware) $(state ref) $(state commit)"
    grep -q "Send Comm Live" "$KLOG" && grep -q "^SimSerial: LIVE" "$PLOG" || { echo "FAIL  no handshake"; exit 1; }
}

stop_emulator() {
    "$ROOT/kickem" stop --out "$RUN" >/dev/null 2>&1
    for f in kick.log panel.log; do [ -f "$RUN/$f" ] && cp "$RUN/$f" "$OUT/$f"; done
    return 0
}

state() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))' "$RUN/state.json" "$1"; }
alive() { kill -0 "$KPID" 2>/dev/null; }
kbuild() { state kick_build; }

# The probe's clock: seconds of master output written so far (32-bit float stereo, 48 kHz),
# measured from the WAV's data chunk, which the probe flushes every 50 ms.
at() {
    if [ -z "${PROBE_OFFSET:-}" ]; then
        PROBE_OFFSET=$(python3 - "$PROBE" <<'EOF'
import struct, sys
d = open(sys.argv[1], 'rb').read(512)
i = d.find(b'data')
print(i + 8 if i >= 0 else 104)
EOF
)
    fi
    echo "$(( ( $(stat -f%z "$PROBE" 2>/dev/null || stat -c%s "$PROBE") - PROBE_OFFSET ) / 8 ))" | awk '{ printf "%.3f", $1 / 48000 }'
}

# Waits (up to 20 s) for a line matching a basic regex in the app log; then a settle time, since
# keys are ignored while a project is still loading (KickHome::handleCommand).
wait_loaded() {
    i=0
    until grep -q "$1" "$KLOG"; do i=$((i + 1)); [ "$i" -ge 80 ] && return 1; sleep 0.25; done
    sleep "${2:-2}"
}

# Lines of a log after the marker "<prefix>: echo MARK <name>" (kickem_ctl echo writes both).
after_k() { awk -v m="TestControl: echo MARK $1" 'index($0, m) == 1 { f = 1 } f' "$KLOG"; }
after_p() { awk -v m="PanelControl: echo MARK $1" 'index($0, m) == 1 { f = 1 } f' "$PLOG"; }
