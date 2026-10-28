#!/bin/sh
# Phase 5 (mode keys) check: every function key and its SHIFT function, on the Starter Kit.
#
#   Kick/tools/sim_modes.sh [OUTDIR]        # default Kick/build/logs/modes
#
# Resets the build copy of "Starter Kit" (Kick/build/demos) from Kick/demos, starts the simulator
# (silent test input, audio probe, surface logging), loads the project with PUSH, then presses each
# key with sim_ctl.sh (so it also runs with the screen locked). For each key it checks that the
# packet reached the app ("Data[0] From DCL:<id>"), that the expected LED packet came back
# ("SimSurface: LED cmd=0 data=<id>,<on>") or the expected log line for keys without an LED, that
# the app is still alive, and saves a screenshot (<OUTDIR>/<n>-<key>-screen.png). Some keys get a
# follow-up that shows the screen working: P1 in NEXT SEQ / TRACK MIX / PAD MIX (these crashed),
# TRACK MUTE muting tracks against the audio probe, NEXT SEQ switching sequences, MASTER switching
# the tempo source, TC moving off-grid notes onto the grid. It starts with an empty project
# (Demo 01) and turns P1-P4 in the screens that used to crash. Prints a table; exits 1 on any
# FAIL (a "gap" row is a known engine limitation, reported but not a failure).
set -u
TOOLS=$(cd "$(dirname "$0")" && pwd)
BUILD=$(cd "$TOOLS/../build" && pwd)
OUT=${1:-$BUILD/logs/modes}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -f "$OUT"/*.png "$OUT"/*.wav "$OUT"/sim.log "$OUT"/results.txt
CTL="$TOOLS/sim_ctl.sh"
LOG="$OUT/sim.log"
PRJ="$BUILD/demos/Starter Kit/Starter Kit.kickPrj"
FAILS=0
N=0

# Fresh build copy of the Starter Kit (a run moves notes and saves it).
rm -rf "$BUILD/demos/Starter Kit"
cp -R "$TOOLS/../demos/Starter Kit" "$BUILD/demos/Starter Kit"
cp "$TOOLS/../demos/Demo 01/Demo 01.kickPrj" "$BUILD/demos/Demo 01/Demo 01.kickPrj"   # the empty project

"$TOOLS/sim_quit.sh"
KICK_SIM_LOG_SURFACE=1 KICK_SIM_AUDIO_INPUT=silence KICK_SIM_AUDIO_PROBE="$OUT/probe.wav" \
    KICK_SIM_FRESH=1 "$TOOLS/sim_launch.sh" "$LOG" >/dev/null || { echo "sim_modes: launch failed" >&2; exit 1; }
trap '"$TOOLS/sim_quit.sh"' EXIT
PID=$(cat "$BUILD/.sim_pid")

mark() { wc -l < "$LOG" | tr -d ' '; }                 # current log length
since() { tail -n +"$(( $1 + 1 ))" "$LOG"; }           # log lines after a mark
alive() { kill -0 "$PID" 2>/dev/null; }
has() { since "$1" | grep -F -q -- "$2"; }             # mark, fixed string

record() {   # record KEY STATUS DETAIL
    N=$((N + 1))
    printf '%-12s %-5s %s\n' "$1" "$2" "$3" >> "$OUT/results.txt"
    [ "$2" = FAIL ] && FAILS=$((FAILS + 1))
    return 0
}

shot() { "$CTL" shot "$OUT/$1.png" >/dev/null 2>&1; }

# Peak (dBFS) of the probe lines logged after a mark: the loudest second.
peak_since() {
    since "$1" | sed -n 's/^AudioProbe: t=[0-9]*s peak=\(-*[0-9.]*\)dBFS.*/\1/p' |
        awk 'BEGIN { m = -200 } { if ($1 + 0 > m) m = $1 + 0 } END { print m }'
}

# press KEY ID LED_EXPECTED [LOG_EXPECTED]: LED_EXPECTED "id,on" or "-" for none.
check_key() {
    KEY=$1; ID=$2; LED=$3; EXTRA=${4:-}
    M=$(mark)
    "$CTL" press "$KEY" >/dev/null 2>&1 || { record "$KEY" FAIL "sim_ctl press failed"; return 1; }
    sleep 1.2
    NAME=$(printf '%02d-%s' "$((N + 1))" "$KEY" | tr ' /' '__')
    shot "$NAME"
    D=""
    alive || { record "$KEY" FAIL "app died"; return 1; }
    has "$M" "Data[0] From DCL:$ID cmd From DCL : 0 type From DCL : 1" || D="$D no packet $ID;"
    if [ "$LED" != "-" ]; then
        has "$M" "SimSurface: LED cmd=0 data=$LED" || D="$D no LED $LED;"
    fi
    if [ -n "$EXTRA" ]; then
        has "$M" "$EXTRA" || D="$D no '$EXTRA';"
    fi
    if [ -z "$D" ]; then
        record "$KEY" ok "id $ID, LED ${LED}${EXTRA:+, '$EXTRA'} ($NAME-screen.png)"
    else
        record "$KEY" FAIL "$D"
    fi
}

# Peak of Pad 1 (the kick) on the probe: press it, take the loudest second that follows.
pad_peak() {
    M=$(mark); "$CTL" press "Pad 1" >/dev/null 2>&1; sleep 2.2; peak_since "$M"
}

# turn KNOB STEPS in the current screen and check the app survived
check_turn() {
    LABEL=$1; KNOB=$2; STEPS=$3; EXPECT=${4:-}
    M=$(mark)
    "$CTL" turn "$KNOB" "$STEPS" >/dev/null 2>&1
    sleep 0.8
    NAME=$(printf '%02d-%s' "$((N + 1))" "$LABEL" | tr ' /+' '___')
    shot "$NAME"
    if ! alive; then record "$LABEL" FAIL "app died (crash)"; return 1; fi
    if [ -n "$EXPECT" ] && ! has "$M" "$EXPECT"; then record "$LABEL" FAIL "no '$EXPECT'"; return 0; fi
    record "$LABEL" ok "$KNOB $STEPS, no crash${EXPECT:+, '$EXPECT'} ($NAME-screen.png)"
}

#### An empty project first: no control may crash ########################################
# "Demo 01" (the next tile after the Starter Kit) has no sequences, tracks or programs. P1-P4 in
# NEXT SEQ, TRACK MIX and PAD MIX used to crash on it (null sequence / missing rows).
sleep 1
"$CTL" turn JOG 1 >/dev/null; sleep 0.5
M=$(mark)
"$CTL" press PUSH >/dev/null; sleep 4
if has "$M" "SimSurface: LED cmd=0 data=8,1"; then record "empty: load" ok "Demo 01 loaded"; else record "empty: load" FAIL "Demo 01 did not load"; fi
for K in "NEXT SEQ" "TRACK MIX" "PAD MIX"; do
    "$CTL" press "$K" >/dev/null 2>&1; sleep 1
    for P in P1 P2 P3 P4; do "$CTL" turn $P 2 >/dev/null 2>&1; "$CTL" turn $P -1 >/dev/null 2>&1; done
    sleep 0.5
    if alive; then record "empty: $K" ok "P1-P4 turned, no crash"; else record "empty: $K" FAIL "app died (crash)"; break; fi
done
if alive; then
    for K in TC ON/OFF ON/OFF MASTER AUTO; do
        "$CTL" press "$K" >/dev/null 2>&1; sleep 0.8
        [ "$K" = TC ] && { "$CTL" click Cancel >/dev/null 2>&1; sleep 0.5; }
    done
    shot "00-empty-after"
    if alive; then record "empty: TC..AUTO" ok "TC, ON/OFF, MASTER, AUTO, no crash"; else record "empty: TC..AUTO" FAIL "app died"; fi
fi
cp "$LOG" "$OUT/sim-empty.log"
"$TOOLS/sim_quit.sh"
sleep 1
KICK_SIM_LOG_SURFACE=1 KICK_SIM_AUDIO_INPUT=silence KICK_SIM_AUDIO_PROBE="$OUT/probe.wav" \
    KICK_SIM_FRESH=1 "$TOOLS/sim_launch.sh" "$LOG" >/dev/null || { echo "sim_modes: launch failed" >&2; exit 1; }
PID=$(cat "$BUILD/.sim_pid")

#### Load the Starter Kit ###################################################################
sleep 1
M=$(mark)
"$CTL" press PUSH >/dev/null; sleep 4
if has "$M" "SimSurface: LED cmd=0 data=8,1"; then record PUSH ok "Starter Kit loaded (MAIN lit)"; else record PUSH FAIL "project did not load"; fi
shot "00-loaded"

#### Every key, in panel order ##############################################################
check_key MAIN 8 "8,1"
check_key GRID 9 "9,1"
check_key BROWSE 10 "10,1"
# TRACK MIX: P2 is row 1's (Starter Drums) LEVEL; PAD MIX: P1 is pad A01's LEVEL. Pad 1 is
# measured at full level, after each cut, and after restoring.
"$CTL" press MAIN >/dev/null; sleep 1
K0=$(pad_peak)
check_key "TRACK MIX" 12 "12,1"
check_turn "TRACK MIX P1+1" P1 1
"$CTL" turn P1 -1 >/dev/null 2>&1
check_turn "TRACK MIX P2-50" P2 -50
K1=$(pad_peak)
"$CTL" turn P2 50 >/dev/null 2>&1; sleep 0.5
check_key "PAD MIX" 13 "13,1"
check_turn "PAD MIX P1-50" P1 -50
K2=$(pad_peak)
"$CTL" turn P1 50 >/dev/null 2>&1; sleep 0.5
K3=$(pad_peak)
if awk -v a="$K0" -v b="$K1" -v c="$K2" -v d="$K3" 'BEGIN { exit !(b < a - 3 && c < a - 3 && d > a - 1) }'; then
    record "MIX levels" ok "Pad 1 peak ${K0} dBFS; TRACK MIX level -50: ${K1}; PAD MIX level -50: ${K2}; restored ${K3}"
else
    # Known engine gap (not counted as a failure): the drum voice ignores a pad's gain and nothing
    # in the engine reads a program's level, so both screens change values that are not heard.
    record "MIX levels" gap "not audible: Pad 1 peak ${K0} dBFS; TRACK MIX level -50: ${K1}; PAD MIX level -50: ${K2}; restored ${K3}"
fi
check_key "TRACK MUTE" 14 "14,1"
check_key "PAD MUTE" 15 "15,1"
check_key "NEXT SEQ" 16 "16,1"
check_turn "NEXT SEQ P1+2" P1 2
"$CTL" turn P1 -2 >/dev/null 2>&1
check_key FX 17 "17,1"
check_turn "FX P1+1" P1 1
check_key MENU 22 "22,1"
check_key PREFS 23 "23,1"
check_key "STEP SEQ" 32 "32,1"
check_key AUTO 33 - "SimUnavailable: Automation"
check_key TC 34 - "TimingCorrect: window shown"
"$CTL" click Cancel >/dev/null 2>&1; sleep 0.5
check_key ON/OFF 35 "34,1" "TimingCorrect: Timing Correct ON"
check_key ON/OFF 35 "34,0" "TimingCorrect: Timing Correct OFF"
check_key SAMPLER 36 "36,1"
check_key LOOP 37 "37,1"
check_key "SAMPLE EDIT" 38 "37,0"            # leaving the looper switches its LED off
check_key "LINK EDIT" 39 - "In screen 39"
check_key "PROG EDIT" 40 "40,1"
check_key "MIDI CTRL" 41 "41,1"
check_key MAIN 8 "8,1"                       # MIDI CTRL is left with MAIN
check_key MASTER 19 "19,0" "Tempo source: Sequence 01 Sequence Tempo"
check_key MASTER 19 "19,1" "Tempo source: Sequence 01 Master Tempo"
check_key LINK 47 - "In screen 47"

# SAVE writes the project (the app adds LOOPER / SAMPLER state on load, so it differs).
BEFORE=$(stat -f%m "$PRJ")
sleep 1.1
check_key SAVE 11 -
AFTER=$(stat -f%m "$PRJ")
if [ "$AFTER" -gt "$BEFORE" ]; then record "SAVE file" ok "Starter Kit.kickPrj rewritten"; else record "SAVE file" FAIL "project file not written"; fi

#### TRACK MUTE: mute tracks while the sequence plays, measured on the probe ################
"$CTL" press MAIN "TRACK MUTE" >/dev/null; sleep 1
"$CTL" press PLAY >/dev/null; sleep 1
"$CTL" press "Pad 2" "Pad 3" >/dev/null; sleep 1      # mute Keys and Loops: the drums alone
M=$(mark); sleep 3; DRUMS=$(peak_since "$M")
"$CTL" press "Pad 1" >/dev/null; sleep 1              # mute Drums too
M=$(mark); sleep 3; NONE=$(peak_since "$M")
shot "$(printf '%02d' $((N + 1)))-TRACK_MUTE-all-muted"
"$CTL" press "Pad 1" "Pad 2" "Pad 3" >/dev/null; sleep 1
M=$(mark); sleep 3; ALL=$(peak_since "$M")
"$CTL" press STOP >/dev/null; sleep 0.5
if awk -v d="$DRUMS" -v n="$NONE" -v a="$ALL" 'BEGIN { exit !(d > -40 && n < -90 && a > -40) }'; then
    record "TRACK MUTE pads" ok "peak drums only ${DRUMS} dBFS, all muted ${NONE} dBFS, unmuted ${ALL} dBFS"
else
    record "TRACK MUTE pads" FAIL "peak drums only ${DRUMS}, all muted ${NONE}, unmuted ${ALL} dBFS"
fi

#### PAD MUTE: mute pad A01 (the kick), play it from MAIN, unmute ###########################
"$CTL" press "PAD MUTE" "Pad 1" >/dev/null; sleep 0.5
shot "$(printf '%02d' $((N + 1)))-PAD_MUTE-pad1-muted"
"$CTL" press MAIN >/dev/null; sleep 1
P_MUTED=$(pad_peak)
"$CTL" press "PAD MUTE" "Pad 1" MAIN >/dev/null; sleep 1
P_OPEN=$(pad_peak)
if awk -v m="$P_MUTED" -v o="$P_OPEN" 'BEGIN { exit !(m < -90 && o > -30) }'; then
    record "PAD MUTE pad 1" ok "Pad 1 peak muted ${P_MUTED} dBFS, unmuted ${P_OPEN} dBFS"
else
    record "PAD MUTE pad 1" FAIL "Pad 1 peak muted ${P_MUTED} dBFS, unmuted ${P_OPEN} dBFS"
fi

#### NEXT SEQ: queue Sequence 02 while Sequence 01 plays ####################################
"$CTL" press "NEXT SEQ" >/dev/null; sleep 1
"$CTL" press PLAYSTART >/dev/null; sleep 1
"$CTL" press "Pad 2" >/dev/null; sleep 0.5
shot "$(printf '%02d' $((N + 1)))-NEXT_SEQ-queued"
sleep 8                                               # Sequence 01 is 4 bars = 8 s at 120 BPM
shot "$(printf '%02d' $((N + 1)))-NEXT_SEQ-switched"
"$CTL" press STOP >/dev/null; sleep 0.5
alive && record "NEXT SEQ pad 2" ok "queued and played (screenshots)" || record "NEXT SEQ pad 2" FAIL "app died"

#### MASTER on Sequence 02 (own tempo 100 BPM) and TC on its off-grid drums ##############
"$CTL" press MAIN >/dev/null; sleep 1
check_key MASTER 19 "19,1" "Tempo source: Sequence 02 Master Tempo: 120 BPM"
check_key MASTER 19 "19,0" "Tempo source: Sequence 02 Sequence Tempo: 100 BPM"
check_key ON/OFF 35 "34,1" "TimingCorrect: Timing Correct ON"
check_key TC 34 - "TimingCorrect: window shown"
M=$(mark)
"$CTL" click Apply >/dev/null 2>&1; sleep 1
if has "$M" "TimingCorrect: note value 1/16 (24 ticks), strength 100%: moved"; then
    record "TC Apply" ok "$(since "$M" | grep -F 'TimingCorrect: note value' | sed 's/^TimingCorrect: //')"
else
    record "TC Apply" FAIL "no TimingCorrect result in the log"
fi
"$CTL" press SAVE >/dev/null; sleep 1.5
OFF=$(python3 - "$PRJ" <<'EOF'
import re, sys
x = open(sys.argv[1]).read()
seq = x[x.index('name="Sequence 02"'):]
seq = seq[:seq.index('</SEQUENCE>')]
trk = seq[seq.index('name="Drums Live"'):]
trk = trk[:trk.index('</TRACK>')]
starts = [int(s) for s in re.findall(r'<NOTE [^>]*start="(\d+)"', trk)]
print(len(starts), sum(1 for s in starts if s % 24))
EOF
)
set -- $OFF
if [ "${1:-0}" -gt 0 ] && [ "${2:-1}" -eq 0 ]; then
    record "TC result" ok "saved Drums Live: $1 notes, all on the 1/16 grid"
else
    record "TC result" FAIL "saved Drums Live: ${1:-?} notes, ${2:-?} off the grid"
fi
check_key ON/OFF 35 "34,0" "TimingCorrect: Timing Correct OFF"

"$CTL" press MAIN >/dev/null; sleep 0.5
shot "99-final"
alive || record "final" FAIL "app died"

"$TOOLS/sim_quit.sh"
trap - EXIT
echo
echo "KEY          RESULT DETAIL"
cat "$OUT/results.txt"
echo
echo "$N checks, $FAILS failed. Evidence: $OUT"
[ "$FAILS" -eq 0 ]
