#!/bin/sh
# Scaled-screen check: the app's full-screen popups (loading spinner, dialogs, nested dialogs)
# and its notification bubbles must appear over the simulated screen, scaled with it, never as
# separate windows at the top-left of the Mac's display. Panel controls must still work while a
# dialog is modal, as the hardware keys do on the unit. A touch on a list row selects that row
# (the lists measured touches in desktop pixels, which the simulator scales).
#
#   Kick/tools/sim_overlays.sh [OUTDIR]      # default OUTDIR: Kick/build/logs/overlays
#
# Exit status 0 when every check passes, 1 otherwise. Evidence in OUTDIR: sim.log and
# screenshots (dialog.png, nested.png, bubble.png, list.png). Runs through sim_ctl.sh, so it
# also works with the Mac's screen locked, except the list-row checks: lists take touches
# through a mouse listener, which only a real click reaches (sim_click.sh: Accessibility
# permission, unlocked screen). Those are reported as SKIP when no real click can be made.
# Build KT_KickEM first.
set -u
TOOLS=$(cd "$(dirname "$0")" && pwd)
KICK=$(cd "$TOOLS/.." && pwd)
OUT=${1:-$KICK/build/logs/overlays}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
CTL="$TOOLS/sim_ctl.sh"
LOG="$OUT/sim.log"
FAILS=0

say()  { printf '%s\n' "$*"; }
pass() { say "PASS  $*"; }
fail() { say "FAIL  $*"; FAILS=$((FAILS + 1)); }

# "sim_ctl.sh windows" lines: KIND<TAB>TYPE<TAB>X Y W H (bounds on the Mac's display).
# check_overlays WHAT TYPE: one or more TYPE overlays, each inside the screen, and no window on
# the desktop besides the panel.
check_overlays() {
    W=$("$CTL" windows) || { fail "$1: windows command failed"; return; }
    printf '%s\n' "$W" > "$OUT/windows-$(printf '%s' "$1" | tr ' /' '__').txt"
    printf '%s\n' "$W" | awk -F'\t' -v what="$1" -v type="$2" '
        $1 == "screen"  { split($3, s, " ") }
        $1 == "desktop" { desk = desk " " $2 }
        $1 == "overlay" && index($2, type) { n++; split($3, b, " "); ob[n] = $3; bx[n] = b[1]; by[n] = b[2]; bw[n] = b[3]; bh[n] = b[4] }
        END {
            if (desk != "") { print "FAIL  " what ": separate desktop window(s):" desk; exit 1 }
            if (n == 0)     { print "FAIL  " what ": no " type " over the screen"; exit 1 }
            for (i = 1; i <= n; i++)
                if (bx[i] < s[1] || by[i] < s[2] || bx[i] + bw[i] > s[1] + s[3] || by[i] + bh[i] > s[2] + s[4]) {
                    print "FAIL  " what ": " type " at " ob[i] " is outside the screen at " s[1] " " s[2] " " s[3] " " s[4]; exit 1 }
            print "PASS  " what ": " n " " type " inside the screen (" s[1] " " s[2] " " s[3] " " s[4] ")"
        }' && return
    FAILS=$((FAILS + 1))
}

# check_none WHAT: no overlay left over the screen and no extra desktop window.
check_none() {
    W=$("$CTL" windows) || { fail "$1: windows command failed"; return; }
    if printf '%s\n' "$W" | grep -q '^overlay\|^desktop'; then
        fail "$1: still showing: $(printf '%s\n' "$W" | grep '^overlay\|^desktop' | cut -f1,2 | tr '\n\t' '  ')"
    else
        pass "$1"
    fi
}

# Every titled component called NAME is on the screen (not in a popup window).
check_on_screen() {
    L=$("$CTL" list "$1")
    if [ -n "$L" ] && ! printf '%s\n' "$L" | grep -qv '	screen$'; then pass "'$1' is part of the screen"; else fail "'$1' not on the screen: $L"; fi
}

trap '"$TOOLS/sim_quit.sh"' EXIT
trap 'exit 1' INT TERM

say "== launch"
PID=$(KICK_SIM_FRESH=1 KICK_SIM_AUDIO_INPUT=silence "$TOOLS/sim_launch.sh" "$LOG") || { say "FAIL  launch, see $LOG"; exit 1; }
pass "launched, pid $PID"

#### 1. Loading spinner (PUSH loads Starter Kit; loading runs on a thread, the UI stays live) ####
"$CTL" press PUSH >/dev/null || fail "PUSH not delivered"
check_overlays "loading spinner" fsPopup
i=0
until grep -q '^SimProject: loaded' "$LOG"; do
    i=$((i + 1)); [ "$i" -ge 60 ] && { fail "project did not load"; exit 1; }
    sleep 0.5
done
# The spinner closes 200 ms after the engine reports the project loaded (kick_RecentProject_Utils.cpp).
i=0
while "$CTL" windows | grep -q '^overlay'; do
    i=$((i + 1)); [ "$i" -ge 40 ] && break
    sleep 0.25
done
say "      (spinner closed $((i * 250)) ms after the load was logged)"
check_none "spinner gone after the load"

#### 2. A dialog (TC opens TIMING CORRECT) ######################################################
"$CTL" press TC >/dev/null || fail "TC not delivered"
sleep 0.8
check_overlays "TIMING CORRECT dialog" dialogDisplayer
check_on_screen Apply
"$CTL" shot "$OUT/dialog.png" >/dev/null

# A panel key clicked while the dialog is modal reaches the app, as the hardware key would.
START=$(( $(wc -l < "$LOG") + 1 ))
if "$CTL" click TC >/dev/null 2>"$OUT/click.err"; then
    sleep 0.3
    if tail -n +"$START" "$LOG" | grep -q 'Data\[0\] From DCL:34 '; then pass "panel key clicked under a modal dialog reaches the app"
    else fail "panel key click under the dialog sent no packet"; fi
else
    fail "panel key click under the dialog: $(cat "$OUT/click.err")"
fi
"$CTL" click Cancel >/dev/null || fail "Cancel not clickable"
sleep 0.5
check_none "dialog closed by Cancel"

#### 3. A nested dialog (PROGRAMS opens PLAYHEAD CONTROL MENU, PUSH on PROGRAM opens PROGRAM) ###
"$CTL" click PROGRAMS >/dev/null || fail "PROGRAMS not clickable"
sleep 0.8
check_overlays "PLAYHEAD CONTROL MENU dialog" dialogDisplayer
"$CTL" tap 600 400 >/dev/null || fail "tap on PROGRAM failed" # focuses the PROGRAM list box
"$CTL" press PUSH >/dev/null || fail "PUSH not delivered"
sleep 0.8
N=$("$CTL" windows | grep -c '^overlay')
[ "$N" -eq 2 ] && pass "nested PROGRAM dialog opened over the first" || fail "expected 2 stacked dialogs, saw $N"
check_overlays "nested dialogs" dialogDisplayer
"$CTL" shot "$OUT/nested.png" >/dev/null
"$CTL" click Cancel >/dev/null; sleep 0.4
"$CTL" click Cancel >/dev/null; sleep 0.4
check_none "both dialogs closed"

#### 4. A notification bubble (ON/OFF = SHIFT + TC toggles timing correct) ######################
"$CTL" press ON/OFF >/dev/null || fail "ON/OFF not delivered"
sleep 0.3
check_overlays "notification bubble" bubbleNotification
"$CTL" shot "$OUT/bubble.png" >/dev/null
sleep 2.5
check_none "bubble gone after its timeout"
"$CTL" press ON/OFF >/dev/null # back off

#### 5. List rows (BROWSE: the Location list, then the file list of Demos) ######################
"$CTL" press BROWSE >/dev/null || fail "BROWSE not delivered"
sleep 1.5
# row ROW_EXPECTED X Y WHAT: real click at Kick-screen pixel (X,Y), expect that row touched.
row() {
    START=$(( $(wc -l < "$LOG") + 1 ))
    "$TOOLS/sim_click.sh" --at "$2" "$3" >/dev/null 2>"$OUT/sim_click.err" || return 2
    sleep 1
    GOT=$(tail -n +"$START" "$LOG" | sed -n 's/^SimTable: .* touched row //p' | tail -1)
    if [ "$GOT" = "$1" ]; then pass "$4: touch selects row $1"; else fail "$4: expected row $1, touched row '${GOT:-none}'"; fi
}
if row 2 83 323 "Location list, Demos"; then
    row 4 460 428 "Demos file list, Demo 03"
    "$CTL" shot "$OUT/list.png" >/dev/null
else
    say "SKIP  list rows: no real click possible ($(head -1 "$OUT/sim_click.err"))"
fi

kill -0 "$PID" 2>/dev/null && pass "simulator still running" || fail "simulator exited"

say ""
if [ "$FAILS" -eq 0 ]; then say "OVERLAYS OK (log and screenshots in $OUT)"; exit 0; fi
say "OVERLAYS FAILED: $FAILS check(s), see $OUT"
exit 1
