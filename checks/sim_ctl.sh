#!/bin/sh
# Drive the running simulator from inside the app: no accessibility, no window server.
#
#   Kick/tools/sim_ctl.sh press NAME...     # front-panel controls by printed name ("Pad 1", MAIN,
#                                           # PUSH, PLAY/STOP/RECORD/OVERDUB/PLAYSTART or "key 45";
#                                           # a caption such as GRID or PREFS
#                                           # performs that SHIFT function)
#   Kick/tools/sim_ctl.sh down NAME [VEL]   # press a pad or key and keep it held (pads: velocity 1..127)
#   Kick/tools/sim_ctl.sh up NAME           # release it
#   Kick/tools/sim_ctl.sh hold NAME SECS [VEL]  # down, wait SECS, up (answers after the release)
#   Kick/tools/sim_ctl.sh click NAME...     # on-screen components by title ("WiFi", "Resources")
#   Kick/tools/sim_ctl.sh tap X Y           # touch pixel (X,Y) of the 1024x600 Kick screen
#   Kick/tools/sim_ctl.sh clickat NAME FX FY  # touch a titled component at a fraction of its size
#                                           # (e.g. clickat ENABLE 0.75 0.6 = the On half)
#   Kick/tools/sim_ctl.sh turn KNOB STEPS   # encoder P1..P4 or JOG, signed detents (one packet each)
#   Kick/tools/sim_ctl.sh slider VALUE      # the slider, 0..100
#   Kick/tools/sim_ctl.sh shot OUT.png      # render the panel (and popups as OUT-<n>.png)
#   Kick/tools/sim_ctl.sh list [FILTER]     # titled components, printed from the app log
#   Kick/tools/sim_ctl.sh windows           # desktop windows, popups over the screen, the screen
#   Kick/tools/sim_ctl.sh repeat MS N NAME  # press a panel control N times, MS ms apart (timed in the app)
#   Kick/tools/sim_ctl.sh status            # transport state, BAR:BEAT:TICK, tempo, record mode
#   Kick/tools/sim_ctl.sh echo TEXT         # write a marker line into the app log
#
# This works while the Mac's screen is locked, asleep or the window is covered, which
# sim_press.sh / sim_click.sh / sim_shot.sh cannot. sim_launch.sh starts the app with
# KICK_SIM_CONTROL pointing at Kick/build/.sim_ctl; this script appends a numbered command
# there and waits (KICK_SIM_CTL_TIMEOUT seconds, default 15) for the app's
# "SimControl: ok <n>" in the log. Exit status 1 on an error or timeout.
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
BUILD=$(cd "$TOOLS/../build" && pwd)
CTL=${KICK_SIM_CONTROL:-$BUILD/.sim_ctl}
LOG=${KICK_SIM_LOG:-$(cat "$BUILD/.sim_log" 2>/dev/null || echo "$BUILD/logs/sim.log")}
TIMEOUT=${KICK_SIM_CTL_TIMEOUT:-15}
PID=${KICK_SIM_PID:-$(cat "$BUILD/.sim_pid" 2>/dev/null || pgrep -x KT_KickEM | head -1)}
[ -n "$PID" ] && kill -0 "$PID" 2>/dev/null || { echo "KT_KickEM is not running" >&2; exit 1; }
grep -q "SimControl: listening" "$LOG" 2>/dev/null || { echo "sim_ctl: the app is not listening (start it with sim_launch.sh; log $LOG)" >&2; exit 1; }

send() {   # send "<command line>"; waits for the ack
    SEQ=$(( $(cat "$BUILD/.sim_ctl_seq" 2>/dev/null || echo 0) + 1 ))
    echo "$SEQ" > "$BUILD/.sim_ctl_seq"
    START=$(wc -l < "$LOG")
    printf '%s %s\n' "$SEQ" "$1" >> "$CTL"
    i=0
    while :; do
        R=$(tail -n +"$((START + 1))" "$LOG" | grep -E "^SimControl: (ok|error) $SEQ( |\$)" | head -1 || true)
        [ -n "$R" ] && break
        kill -0 "$PID" 2>/dev/null || { echo "sim_ctl: KT_KickEM exited" >&2; exit 1; }
        i=$((i + 1))
        [ "$i" -ge $((TIMEOUT * 10)) ] && { echo "sim_ctl: no answer to '$1' after ${TIMEOUT}s (UI blocked?)" >&2; exit 1; }
        sleep 0.1
    done
    case "$R" in "SimControl: error"*) echo "sim_ctl: $R" >&2; exit 1 ;; esac
    OUTPUT=$(tail -n +"$((START + 1))" "$LOG" | grep "^SimControl: " | grep -v -E "^SimControl: (ok|error) " || true)
}

q() { printf '"%s"' "$1"; }   # quote one argument for the app's tokenizer

# Transport keys carry icons, not text: accept their names as sim_press.sh does.
key_name() {
    case "$1" in
        RECORD|REC) echo "key 42" ;;
        OVERDUB)    echo "key 43" ;;
        STOP)       echo "key 44" ;;
        PLAY)       echo "key 45" ;;
        PLAYSTART|"PLAY START") echo "key 46" ;;
        *)          echo "$1" ;;
    esac
}

CMD=$1; shift || true
case "$CMD" in
    press)
        for NAME in "$@"; do send "press $(q "$(key_name "$NAME")")"; sleep 0.2; done ;;
    down) send "down $(q "$(key_name "$1")") ${2:-}" ;;
    up)   send "up $(q "$(key_name "$1")")" ;;
    hold)
        TIMEOUT=$(( TIMEOUT + ${2%.*} + 1 ))
        send "hold $(q "$(key_name "$1")") $2 ${3:-}" ;;
    click)
        for NAME in "$@"; do send "click $(q "$NAME")"; sleep 0.2; done ;;
    tap)  send "tap $1 $2" ;;
    clickat) send "clickat $(q "$1") $2 $3" ;;
    turn) send "turn $1 $2" ;;
    slider) send "slider $1" ;;
    shot)
        case "$1" in /*) OUT=$1 ;; *) OUT="$(pwd)/$1" ;; esac
        send "shot $(q "$OUT")"
        printf '%s\n' "$OUTPUT" | sed -n 's/^SimControl: shot \([^ ]*\).*/\1/p' ;;
    list) send "list $(q "$*")"; printf '%s\n' "$OUTPUT" | sed -n 's/^SimControl: item //p' ;;
    repeat)   # repeat MS COUNT NAME: COUNT presses of NAME, MS apart, timed inside the app
        MS=$1; N=$2; shift 2
        TIMEOUT=$(( TIMEOUT + MS * N / 1000 + 1 ))
        send "repeat $MS $N $(q "$(key_name "$*")")"
        printf '%s\n' "$OUTPUT" | sed -n 's/^SimControl: repeat //p' ;;
    windows) send "windows"; printf '%s\n' "$OUTPUT" | sed -n 's/^SimControl: window //p' ;;
    status) send "status"; printf '%s\n' "$OUTPUT" | sed -n 's/^SimControl: status //p' ;;
    echo) send "echo $*" ;;
    *) sed -n '2,28p' "$0" >&2; exit 2 ;;
esac
