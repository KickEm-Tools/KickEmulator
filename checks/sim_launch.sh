#!/bin/sh
# Launch the simulator from Kick/build and wait for the control-surface handshake.
#
#   Kick/tools/sim_launch.sh [LOGFILE]          # default: Kick/build/logs/sim.log
#
# Prints the simulator's pid on success. The app's stdout/stderr go to LOGFILE.
# The opt-in engine tooling is passed through from the environment:
#
#   KICK_SIM_AUDIO_PROBE=<file.wav>   write the master output to a WAV, log peak/RMS per second
#   KICK_SIM_AUDIO_INPUT=silence|tone|tone:<hz>|pulse|<file>|device   replace the audio input
#       with a test source. Unset, this script uses "silence": the app never opens a path from the
#       microphone to the speakers by itself, but the SAMPLER / LOOPER MONITOR buttons do, and on a
#       laptop that howls. Pass "device" for the real input (headphones).
#
#   KICK_SIM_FRESH=1                  first restore Kick/build/demos and Kick/build/templates from the
#                                     sources: SAVE writes into those build copies, so a check that
#                                     saved (e.g. a pad level of 0) would otherwise leak into the next.
#
# The in-app scripting channel used by sim_ctl.sh is always on: KICK_SIM_CONTROL defaults to
# Kick/build/.sim_ctl (emptied here), and the log path is recorded in Kick/build/.sim_log.
#
# Only one KT_KickEM can run on a machine: the app refuses a second instance, and the press
# helper and the audio device assume one. This script takes a machine-wide lock
# (KICK_SIM_LOCK, default /tmp/kt_kickem.lock) and waits up to KICK_SIM_LOCK_WAIT seconds
# (default 1800) for another session's simulator to quit. Always stop it with sim_quit.sh.
set -e

TOOLS=$(cd "$(dirname "$0")" && pwd)
BUILD="$TOOLS/../build"
APP="$BUILD/KT_KickEM.app/Contents/MacOS/KT_KickEM"
LOCK=${KICK_SIM_LOCK:-/tmp/kt_kickem.lock}
WAIT=${KICK_SIM_LOCK_WAIT:-1800}
LOG=${1:-$BUILD/logs/sim.log}

[ -x "$APP" ] || { echo "sim_launch: $APP not built" >&2; exit 1; }

alive() { [ -n "$1" ] && kill -0 "$1" 2>/dev/null; }

# Acquire the lock. Its owner is the simulator pid written into it; a dead owner is stale.
waited=0
while :; do
    if mkdir "$LOCK" 2>/dev/null; then
        # Own it at once (this shell, replaced by the app's pid below), so a waiting
        # launcher never mistakes the fresh lock for a stale one.
        echo "$$" > "$LOCK/pid"
        if [ -z "$(pgrep -x KT_KickEM)" ]; then
            break
        fi
        rm -rf "$LOCK"   # someone launched without the lock; wait for it
    else
        owner=$(cat "$LOCK/pid" 2>/dev/null || true)
        if [ -n "$owner" ] && ! alive "$owner"; then
            rm -rf "$LOCK"
            continue
        fi
        if [ -z "$owner" ] && [ "$waited" -ge 30 ] && [ -z "$(pgrep -x KT_KickEM)" ]; then
            rm -rf "$LOCK"   # a launcher died before writing the pid
            continue
        fi
    fi
    [ "$waited" -ge "$WAIT" ] && { echo "sim_launch: another KT_KickEM is still running after ${WAIT}s" >&2; exit 1; }
    [ "$waited" -eq 0 ] && echo "sim_launch: waiting for another KT_KickEM to quit..." >&2
    sleep 5
    waited=$((waited + 5))
done

if [ -n "${KICK_SIM_FRESH:-}" ]; then
    for d in demos templates; do
        rm -rf "${BUILD:?}/$d" && cp -R "$TOOLS/../$d" "$BUILD/$d"
    done
fi

mkdir -p "$(dirname "$LOG")"
export KICK_SIM_AUDIO_INPUT=${KICK_SIM_AUDIO_INPUT:-silence}
cd "$BUILD"
export KICK_SIM_CONTROL=${KICK_SIM_CONTROL:-$PWD/.sim_ctl}
[ -p "$KICK_SIM_CONTROL" ] && rm -f "$KICK_SIM_CONTROL"   # never block on a stale FIFO
: > "$KICK_SIM_CONTROL"
case "$LOG" in /*) echo "$LOG" > .sim_log ;; *) echo "$OLDPWD/$LOG" > .sim_log ;; esac
"$APP" > "$LOG" 2>&1 &
PID=$!
echo "$PID" > "$LOCK/pid"
echo "$PID" > "$BUILD/.sim_pid"

# Handshake: the app logs "Send Comm Live" once the simulated surface has answered.
i=0
until grep -q "Send Comm Live" "$LOG" 2>/dev/null; do
    alive "$PID" || { echo "sim_launch: KT_KickEM exited during startup, see $LOG" >&2; rm -rf "$LOCK"; exit 1; }
    i=$((i + 1))
    [ "$i" -ge 120 ] && { echo "sim_launch: no handshake after 60s, see $LOG" >&2; exit 1; }
    sleep 0.5
done
echo "$PID"
