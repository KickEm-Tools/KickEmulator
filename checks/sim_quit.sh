#!/bin/sh
# Stop the simulator started by sim_launch.sh and release the machine-wide lock.
TOOLS=$(cd "$(dirname "$0")" && pwd)
BUILD="$TOOLS/../build"
LOCK=${KICK_SIM_LOCK:-/tmp/kt_kickem.lock}

PID=$(cat "$BUILD/.sim_pid" 2>/dev/null || true)
if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    kill -TERM "$PID" 2>/dev/null || true
    i=0
    while kill -0 "$PID" 2>/dev/null && [ "$i" -lt 20 ]; do sleep 0.5; i=$((i + 1)); done
    kill -0 "$PID" 2>/dev/null && kill -KILL "$PID" 2>/dev/null
fi
rm -f "$BUILD/.sim_pid"
[ "$(cat "$LOCK/pid" 2>/dev/null)" = "$PID" ] && rm -rf "$LOCK"
exit 0
