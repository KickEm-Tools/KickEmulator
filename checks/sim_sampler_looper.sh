#!/bin/sh
# Phase 4 check: SAMPLER records, SAMPLE EDIT trims, the take is assigned to a pad and plays,
# and the LOOPER records, loops in time and overdubs. Everything through sim_ctl.sh, so it
# also runs with the screen locked. Measurements are printed; the evidence (logs, probe WAVs,
# screenshots) is left in OUTDIR.
#
#   Kick/tools/sim_sampler_looper.sh [OUTDIR]        # default Kick/build/logs/phase4
#
# It works on the build copy of "Demo 01" (Kick/build/demos, not the sources): it resets it,
# then creates a sequence, a drum program and a track, records "New Sample.wav" next to it
# and saves the project. Needs python3 with numpy (tools/wav_measure.py).
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
BUILD=$(cd "$TOOLS/../build" && pwd)
OUT=${1:-$BUILD/logs/phase4}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
CTL="$TOOLS/sim_ctl.sh"
M="python3 $TOOLS/wav_measure.py"

# Probe position in seconds (32-bit float stereo at 48 kHz after a small header).
at() { echo "$(( ( $(stat -f%z "$1") - 88 ) / 8 ))" | awk '{ printf "%.3f", $1 / 48000 }'; }

# Fresh copy of Demo 01.
rm -rf "$BUILD/demos/Demo 01/Sequence1" "$BUILD/demos/New Sample.wav" "$BUILD/demos/undo_sampleeditor.wav"
cp "$TOOLS/../demos/Demo 01/Demo 01.kickPrj" "$BUILD/demos/Demo 01/Demo 01.kickPrj"

#### Session 1: 1 kHz tone on the input ####################################################
P1="$OUT/tone-probe.wav"
"$TOOLS/sim_quit.sh"
KICK_SIM_LOG_SURFACE=1 KICK_SIM_AUDIO_INPUT=tone KICK_SIM_AUDIO_PROBE="$P1" "$TOOLS/sim_launch.sh" "$OUT/tone.log" >/dev/null
trap '"$TOOLS/sim_quit.sh"' EXIT

"$CTL" tap 283 150; sleep 3                       # Projects: touch "Demo 01" (second tile; Starter Kit is first)
"$CTL" tap 76 108; "$CTL" tap 993 108; sleep 1 # MAIN: Sequence tab (named " Sequence "), "+"
"$CTL" click Program; "$CTL" tap 993 108; sleep 1  # Program tab, "+" -> CREATE PROGRAM
"$CTL" click Drum; "$CTL" click Apply; sleep 1
"$CTL" click Track; "$CTL" tap 993 108; sleep 1    # Track tab, "+"
"$CTL" tap 33 222; sleep 1                         # the track's program button -> Select Program
"$CTL" click "Program 01"; "$CTL" click Apply; sleep 1

"$CTL" press SAMPLER; sleep 1.5
"$CTL" turn P1 -18; sleep 0.3                      # Max Length 20 s -> 2 s (P1 = seconds)
"$CTL" click recordButton; sleep 3.5               # records, stops by itself at 2 s
"$CTL" shot "$OUT/1-sampler-recorded.png" >/dev/null
T_PREVIEW=$(at "$P1"); "$CTL" click Play; sleep 3  # preview the take
"$CTL" click Save; sleep 0.8                       # KEEP OR DISCARD: Program 01, pad A01
"$CTL" shot "$OUT/2-save-dialog.png" >/dev/null
"$CTL" click Apply; sleep 1.5
cp "$BUILD/demos/New Sample.wav" "$OUT/recorded.wav"

"$CTL" press MAIN; sleep 2.5
T_PAD=$(at "$P1"); "$CTL" press "Pad 1"; sleep 3

"$CTL" press "SAMPLE EDIT"; sleep 1.5
"$CTL" click Chop; sleep 0.5
"$CTL" turn P4 24; sleep 0.5                       # START + 24 x 1000 samples
"$CTL" shot "$OUT/3-trim-start.png" >/dev/null
"$CTL" click Tune; sleep 0.5
"$CTL" click Process; sleep 1                      # Select Function
"$CTL" click Discard; sleep 0.8                    # PROCESS SLICE: discard outside START..END
"$CTL" click Apply; sleep 2.5
"$CTL" shot "$OUT/4-trimmed.png" >/dev/null
cp "$BUILD/demos/New Sample.wav" "$OUT/trimmed.wav"
"$CTL" press SAVE; sleep 1.5                       # SHIFT + BROWSE: save the project
"$TOOLS/sim_quit.sh"

echo "== recorded take"; $M "$OUT/recorded.wav"
echo "== preview (probe)"; $M "$P1" --from "$T_PREVIEW" --to "$(echo "$T_PREVIEW + 3" | bc)" --thr -40
echo "== Pad 1 (probe)"; $M "$P1" --from "$(echo "$T_PAD - 2" | bc)" --to "$T_PAD" --thr -40
$M "$P1" --from "$T_PAD" --to "$(echo "$T_PAD + 3" | bc)" --thr -40
echo "== after Discard (trim)"; $M "$OUT/trimmed.wav"

#### Session 2: pulse train on the input, reloaded project ################################
P2="$OUT/pulse-probe.wav"
KICK_SIM_LOG_SURFACE=1 KICK_SIM_AUDIO_INPUT=pulse:700 KICK_SIM_AUDIO_PROBE="$P2" "$TOOLS/sim_launch.sh" "$OUT/pulse.log" >/dev/null
"$CTL" tap 283 150; sleep 3
T_PAD2=$(at "$P2"); "$CTL" press "Pad 1"; sleep 2.5 # the reloaded program plays the trimmed file
"$CTL" press LOOP; sleep 1.5                        # SHIFT + SAMPLER; LENGTH 4 bars
T_REC=$(at "$P2"); "$CTL" press "key 42"; sleep 26  # RECORD: one pass, then it plays
"$CTL" shot "$OUT/5-looper-playing.png" >/dev/null
T_OD=$(at "$P2"); "$CTL" press "key 43"; sleep 8.5  # OVERDUB for about one pass
T_PL=$(at "$P2"); "$CTL" press "key 45"; sleep 17   # PLAY: punch out, keep looping
"$CTL" shot "$OUT/6-looper-overdubbed.png" >/dev/null
"$CTL" press "key 44"; sleep 1                      # STOP
"$TOOLS/sim_quit.sh"
trap - EXIT

echo "== Pad 1 after reload (probe)"; $M "$P2" --from "$T_PAD2" --to "$(echo "$T_PAD2 + 2.5" | bc)" --thr -40
echo "== looper: RECORD at $T_REC s, OVERDUB at $T_OD s, PLAY at $T_PL s"
$M "$P2" --from "$T_REC" --to "$(echo "$T_PL + 17" | bc)" --onsets
