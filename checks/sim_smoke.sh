#!/bin/sh
# Smoke test: build the simulator, boot it, load the Starter Kit project with PUSH, play four
# bars, stop, and check that the engine made sound and the panel got its LED feedback.
#
#   Kick/tools/sim_smoke.sh [--no-build] [OUTDIR]      # default OUTDIR: Kick/build/logs/smoke
#
# Exit status 0 when every check passes, 1 otherwise. Evidence is left in OUTDIR: the app log
# (sim.log), the master output (probe.wav), and screenshots (loaded*.png, playing*.png).
# Runs through sim_ctl.sh, so it also works with the Mac's screen locked. The audio input is
# replaced by silence (no microphone, no feedback through laptop speakers).
#
# The Projects screen offers the demos first, Starter Kit at the top, but a fresh preference
# set is used so that a recent project cannot take the jog wheel's selection: Kick/build/prefs
# is moved aside for the run and put back afterwards.
set -u
TOOLS=$(cd "$(dirname "$0")" && pwd)
KICK=$(cd "$TOOLS/.." && pwd)
BUILD_FIRST=1
[ "${1:-}" = "--no-build" ] && { BUILD_FIRST=0; shift; }
OUT=${1:-$KICK/build/logs/smoke}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
CTL="$TOOLS/sim_ctl.sh"
LOG="$OUT/sim.log"
FAILS=0

say()  { printf '%s\n' "$*"; }
pass() { say "PASS  $*"; }
fail() { say "FAIL  $*"; FAILS=$((FAILS + 1)); }

# Lines of the log after the marker written by "sim_ctl.sh echo MARKER" (and before the next one).
after()   { sed -n "/^SimControl: echo $1\$/,\$p" "$LOG"; }
between() { sed -n "/^SimControl: echo $1\$/,/^SimControl: echo $2\$/p" "$LOG"; }
# Per-second peaks (dBFS, integer part) from AudioProbe lines on stdin.
peaks()   { sed -n 's/^AudioProbe: t=[0-9]*s peak=\(-*[0-9.]*\)dBFS.*/\1/p'; }

#### 1. Build ##################################################################################
if [ "$BUILD_FIRST" = 1 ]; then
    if [ ! -f "$KICK/build/CMakeCache.txt" ]; then
        say "== configure"
        ( cd "$KICK" && MACOSX_DEPLOYMENT_TARGET=12.0 cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
              -DKICK_SIMULATOR=ON -DTIMESTAMP=OFF -DCPM_AudIO_SOURCE="$KICK/../AudIO" \
              -DCPM_COM_PROTOCOL_SOURCE="$KICK/../ComProtocol" ${CPM_SOURCE_CACHE:+-DCPM_SOURCE_CACHE=$CPM_SOURCE_CACHE} ) > "$OUT/configure.log" 2>&1 \
            || { say "FAIL  configure, see $OUT/configure.log"; exit 1; }
    fi
    say "== build KT_KickEM"
    ( cd "$KICK" && MACOSX_DEPLOYMENT_TARGET=12.0 cmake --build build --target KT_KickEM -j8 ) > "$OUT/build.log" 2>&1 \
        || { say "FAIL  build, see $OUT/build.log"; exit 1; }
fi

PROJECT="$KICK/build/demos/Starter Kit/Starter Kit.kickPrj"
[ -f "$PROJECT" ] || { say "FAIL  $PROJECT is missing (re-run the CMake configure)"; exit 1; }
BPM=$(sed -n 's/.*bpm="\([0-9.]*\)".*/\1/p' "$PROJECT" | head -1)
BPM=${BPM:-120}
FOUR_BARS=$(awk -v b="$BPM" 'BEGIN { printf "%.1f", 4 * 4 * 60 / b }')

#### 2. Boot with fresh preferences ############################################################
PREFS="$KICK/build/prefs"
SAVED=""
if [ -e "$PREFS" ]; then
    SAVED="$OUT/prefs.saved.$$"
    mv "$PREFS" "$SAVED"
fi
cleanup() {
    "$TOOLS/sim_quit.sh"
    if [ -n "$SAVED" ]; then rm -rf "$PREFS"; mv "$SAVED" "$PREFS"; fi
}
trap cleanup EXIT
trap 'exit 1' INT TERM

say "== launch"
rm -f "$OUT/probe.wav"
PID=$(KICK_SIM_FRESH=1 KICK_SIM_AUDIO_INPUT=silence KICK_SIM_AUDIO_PROBE="$OUT/probe.wav" KICK_SIM_LOG_SURFACE=1 \
      "$TOOLS/sim_launch.sh" "$LOG") || { say "FAIL  launch / handshake, see $LOG"; exit 1; }
pass "handshake (Send Comm Live), pid $PID"

#### 3. PUSH loads the Starter Kit ##############################################################
"$CTL" echo SMOKE_LOAD >/dev/null
"$CTL" press PUSH >/dev/null || fail "PUSH not delivered"
i=0
until grep -q "^SimProject: loaded .*/Starter Kit\(/Starter Kit.kickPrj\)\{0,1\}$" "$LOG"; do
    i=$((i + 1)); [ "$i" -ge 40 ] && break; sleep 0.5
done
if grep -q "^SimProject: loaded .*/Starter Kit\(/Starter Kit.kickPrj\)\{0,1\}$" "$LOG"; then pass "PUSH loaded Starter Kit"; else fail "PUSH did not load Starter Kit"; fi
sleep 2
"$CTL" shot "$OUT/loaded.png" >/dev/null
if after SMOKE_LOAD | grep -q "^SimSurface: PAD cmd=6 "; then pass "pad colours sent after load"; else fail "no pad colour packet after load"; fi

QUIET=$(after SMOKE_LOAD | grep "^AudioProbe:" | tail -1 | peaks)
if [ -n "$QUIET" ] && awk -v p="$QUIET" 'BEGIN { exit !(p < -60) }'; then pass "silent before PLAY (peak $QUIET dBFS)"; else fail "not silent before PLAY (peak ${QUIET:-?} dBFS)"; fi

#### 4. PLAY four bars, STOP #####################################################################
"$CTL" echo SMOKE_PLAY >/dev/null
"$CTL" press PLAY >/dev/null || fail "PLAY not delivered"
sleep "$FOUR_BARS"
"$CTL" shot "$OUT/playing.png" >/dev/null
"$CTL" echo SMOKE_STOP >/dev/null
"$CTL" press STOP >/dev/null || fail "STOP not delivered"
sleep 3
"$CTL" echo SMOKE_END >/dev/null
sleep 0.5

kill -0 "$PID" 2>/dev/null && pass "still running" || fail "KT_KickEM exited (crash?)"

#### 5. Checks ###################################################################################
# LEDs: the app lights PLAY when the transport runs and STOP when it stops.
if between SMOKE_PLAY SMOKE_STOP | grep -q "^SimSurface: LED cmd=0 data=45,1"; then pass "PLAY LED on"; else fail "no PLAY LED packet after PLAY"; fi
if between SMOKE_STOP SMOKE_END  | grep -q "^SimSurface: LED cmd=0 data=44,1"; then pass "STOP LED on"; else fail "no STOP LED packet after STOP"; fi

# Audio: every full second of the four bars has sound, nothing clips, and STOP silences it.
PLAYING=$(between SMOKE_PLAY SMOKE_STOP | grep "^AudioProbe:" | sed 1d | peaks)
N=$(printf '%s\n' "$PLAYING" | grep -c .)
LOUD=$(printf '%s\n' "$PLAYING" | awk '$1 > -40' | grep -c .)
MAX=$(printf '%s\n' "$PLAYING" | sort -g | tail -1)
if [ "$N" -ge 3 ] && [ "$LOUD" -eq "$N" ]; then pass "sound during playback: $LOUD/$N seconds above -40 dBFS"; else fail "sound during playback: only $LOUD/$N seconds above -40 dBFS"; fi
if [ -n "$MAX" ] && awk -v p="$MAX" 'BEGIN { exit !(p <= 0) }'; then pass "no clipping (max peak $MAX dBFS)"; else fail "master clipped (max peak ${MAX:-?} dBFS)"; fi
LAST=$(between SMOKE_STOP SMOKE_END | grep "^AudioProbe:" | tail -1 | peaks)
if [ -n "$LAST" ] && awk -v p="$LAST" 'BEGIN { exit !(p < -60) }'; then pass "silent after STOP (peak $LAST dBFS)"; else fail "not silent after STOP (peak ${LAST:-?} dBFS)"; fi

say ""
say "evidence: $OUT (sim.log, probe.wav, loaded*.png, playing*.png)"
if [ "$FAILS" -eq 0 ]; then say "SMOKE OK"; exit 0; fi
say "SMOKE FAILED: $FAILS check(s)"
exit 1
