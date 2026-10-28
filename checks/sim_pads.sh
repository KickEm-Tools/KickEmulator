#!/bin/sh
# Phase 5 check (pads and encoders): bank keys, FULL / HALF LEVEL, 16 LEVELS, NOTE REPEAT /
# LATCH, NOTES, ERASE + pad, BACK, COPY / ERASE in GRID, + / -, the jog wheel, PUSH, P1-P4 and
# the slider, on the Starter Kit. Everything through sim_ctl.sh, so it also runs with the
# screen locked. Prints the measurements and exits non-zero if one fails; the evidence (app
# log with SimSurface packets, probe WAV, screenshots) is left in OUTDIR.
#
#   Kick/tools/sim_pads.sh [OUTDIR]          # default Kick/build/logs/sim_pads
#
# Needs python3 with numpy. Uses the build copy of the Starter Kit and does not save it.
set -u
TOOLS=$(cd "$(dirname "$0")" && pwd)
BUILD=$(cd "$TOOLS/../build" && pwd)
OUT=${1:-$BUILD/logs/sim_pads}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -f "$OUT"/*.png "$OUT/probe.wav" "$OUT/sim.log" "$OUT/marks.txt"
C="$TOOLS/sim_ctl.sh"
LOG="$OUT/sim.log"
PROBE="$OUT/probe.wav"

# Probe position in seconds (32-bit float stereo at 48 kHz after an 88-byte header).
at() { echo "$(( ( $(stat -f%z "$PROBE") - 88 ) / 8 ))" | awk '{ printf "%.3f", $1 / 48000 }'; }
mark() { echo "$1 $(at)" >> "$OUT/marks.txt"; "$C" echo "MARK $1" >/dev/null; }
shot() { "$C" shot "$OUT/$1.png" >/dev/null; }
FAIL=0
check() { # check NAME CONDITION-COMMAND...
    name=$1; shift
    if "$@" >/dev/null 2>&1; then echo "  ok    $name"; else echo "  FAIL  $name"; FAIL=1; fi
}
inlog() { grep -q -- "$1" "$LOG"; }
# Lines of the log after the marker "MARK $1".
after() { awk -v m="SimControl: echo MARK $1" 'index($0, m) == 1 { f = 1 } f' "$LOG"; }
afterhas() { after "$1" | grep -q -- "$2"; }
# The selected track, from the pad colours the app last sent on MAIN with bank A (1 Drums,
# 2 Keys, 3 Loops), and the jog wheel turned until it is the wanted one.
track_now() {
    case "$(grep 'SimSurface: PAD cmd=6 data=' "$LOG" | tail -1 | sed 's/.*data=//' | cut -d, -f1-8)" in
        12,13,14,9,16,12,0,14) echo 1 ;;
        6,6,6,6,6,6,6,6) echo 2 ;;
        12,13,14,9,12,13,14,9) echo 3 ;;
        *) echo 0 ;;
    esac
}
select_track() { # the track list clamps, so: all the way up, then down to track $1
    "$C" turn JOG -3 >/dev/null; sleep 0.6
    [ "$1" -gt 1 ] && "$C" turn JOG $(( $1 - 1 )) >/dev/null
    sleep 0.8
}

"$TOOLS/sim_quit.sh"
KICK_SIM_FRESH=1 KICK_SIM_LOG_NOTES=1 KICK_SIM_LOG_SURFACE=1 KICK_SIM_AUDIO_INPUT=silence KICK_SIM_AUDIO_PROBE="$PROBE" "$TOOLS/sim_launch.sh" "$LOG" >/dev/null || exit 1
trap '"$TOOLS/sim_quit.sh"' EXIT
sleep 2
"$C" press PUSH; sleep 4                              # Projects: load the Starter Kit
shot 00-main

#### Banks A-D, E-H ##########################################################################
mark banks
for B in B C D E F G H A; do "$C" press $B; sleep 0.4; done
shot 01-bank-a

#### FULL LEVEL / HALF LEVEL: Pad 1 (kick) on the Drums track ###############################
sleep 1; mark levels
"$C" hold "Pad 1" 0.1 127; sleep 0.9                  # reference: velocity 127
"$C" hold "Pad 1" 0.1 30; sleep 0.9                   # a soft hit
"$C" press "FULL LEVEL"; "$C" hold "Pad 1" 0.1 30; sleep 0.9
"$C" press "HALF LVL"; shot 02-half-level; "$C" hold "Pad 1" 0.1 30; sleep 0.9
"$C" press "HALF LVL"; "$C" hold "Pad 1" 0.1 64; sleep 0.9   # off again; reference: velocity 64
sleep 0.5

#### 16 LEVELS: Pad 3 (closed hat) spread over the pads ######################################
mark sixteen
"$C" hold "Pad 3" 0.05 100; sleep 0.6
"$C" press "16 LEVELS"; sleep 0.3; shot 03-16-levels
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do "$C" hold "Pad $i" 0.05 100; sleep 0.3; done
"$C" press "16 LEVELS"; sleep 1

#### NOTE REPEAT (held), LATCH, and a plain held pad: Pad 15 (clave) #########################
mark repeat
"$C" down "NOTE REPEAT"; shot 04-note-repeat; "$C" down "Pad 15" 100; sleep 1.0; "$C" up "Pad 15"; "$C" up "NOTE REPEAT"; sleep 1.2
mark latch
"$C" press LATCH; sleep 0.2; shot 05-latch; "$C" hold "Pad 15" 1.0 100; "$C" press LATCH; sleep 1.2
mark plain
"$C" hold "Pad 15" 1.0 100; sleep 1.2

#### Focus: touch LEVEL of track 1, then slider, + / -, jog ##################################
# (A value change can rebuild the row and drop the focus, on the device too: touch again.)
mark focus
"$C" tap 643 197; sleep 0.3
"$C" slider 30; sleep 0.5; shot 06-slider-30           # LEVEL 100 -> 38 (30 % of 127)
"$C" tap 643 197; "$C" press "key 21"; "$C" press "key 21"; sleep 0.5; shot 07-plus-plus  # 40
"$C" tap 643 197; "$C" press "key 27"; sleep 0.5; shot 08-minus                           # 39
"$C" tap 643 197; "$C" turn JOG 3; sleep 0.5; shot 09-jog+3                              # 42
"$C" tap 580 235; sleep 0.3                            # the row itself: focus off (AppTrackDisplay)
select_track 1

#### P1-P4: MAIN Track tab, TRACK MIX, STEP SEQ ##############################################
mark qlinks
"$C" turn P3 5; sleep 0.3; shot 10-main-p3+5            # LEVEL of the first track row
"$C" turn P4 2; sleep 0.3; shot 11-main-p4+2            # TRANSPOSE
"$C" turn P4 -2 >/dev/null
"$C" press "TRACK MIX"; sleep 1.5
"$C" turn P2 -10; "$C" turn P3 -3; sleep 0.3; shot 12-trackmix-p2-p3   # level, pan of strip 1
"$C" press "STEP SEQ"; sleep 1.5
"$C" turn P2 -4; sleep 0.3; shot 13-stepseq-p2           # velocity of step 2 on the page
"$C" press MAIN; sleep 1.5

#### Jog in lists, PUSH, BACK ###############################################################
mark jog
"$C" turn JOG 1; sleep 0.8; shot 14-main-jog+1           # selects track 2 (Keys)
check "jog on MAIN selects the next track" [ "$(track_now)" = 2 ]
"$C" turn JOG -1; sleep 0.8
check "jog back selects the previous track" [ "$(track_now)" = 1 ]
"$C" press BROWSE; sleep 1.5; "$C" turn JOG 2; sleep 0.5; shot 15-browse-jog+2
mark back
"$C" press "key 29"; sleep 1.5; shot 16-back-from-browse
"$C" press "PROG EDIT"; sleep 1.5; "$C" turn P1 2; sleep 0.3; shot 17-progedit-p1
"$C" press "key 29"; sleep 1.5; shot 18-back-from-progedit

#### GRID: select a note (arrow tool, touch the first hat), COPY, then ERASE the copy ##########
mark grid
"$C" press GRID; sleep 1.5; "$C" tap 602 37; sleep 0.3
"$C" tap 180 481; sleep 0.5; shot 19-grid-selected       # A3 (hat) on 1.1 outlined
"$C" press "key 20"; sleep 0.5; shot 20-grid-copy        # a copy right after it, on step 2
"$C" tap 233 481; sleep 0.5; "$C" press ERASE; sleep 0.5; shot 21-grid-erase   # the copy is gone
"$C" press "key 29"; sleep 1.5

#### NOTES (Pad Perform) on the Keys track ##################################################
mark notes
select_track 2                                           # MAIN: track 2 (Keys)
"$C" press NOTES; sleep 1.5; shot 22-notes
for p in 1 2 13; do "$C" hold "Pad $p" 0.3 100; sleep 1.6; done
"$C" press MAIN; sleep 1.5; select_track 1

#### ERASE + pad: Drums track alone (Keys, Loops muted) ######################################
mark erase
"$C" tap 867 336; "$C" tap 867 452; sleep 0.3; shot 23-muted
mark play1; "$C" press PLAYSTART; sleep 4; "$C" press STOP; sleep 1
"$C" down ERASE; "$C" press "Pad 3"; "$C" up ERASE; sleep 0.3
mark play2; "$C" press PLAYSTART; sleep 4; "$C" press STOP; sleep 1
"$C" down ERASE; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do "$C" press "Pad $i"; done; "$C" up ERASE; sleep 0.3
mark play3; "$C" press PLAYSTART; sleep 4; "$C" press STOP; sleep 1
#### MENU: jog through the list, + steps once more, PUSH opens the item ######################
mark menu
"$C" press MENU; sleep 1.5; "$C" turn JOG 2; "$C" press "key 21"; sleep 0.5; shot 19a-menu
"$C" press PUSH; sleep 1.5; shot 19b-menu-push               # item 4: Next Sequence
"$C" press MAIN; sleep 1.5   # (last: NEXT SEQ may switch the sequence)

mark end
shot 24-erased

#### Measurements ###########################################################################
echo "sim_pads: measurements (probe $PROBE)"
python3 - "$PROBE" "$OUT/marks.txt" "$TOOLS/wav_measure.py" "$LOG" <<'EOF' || FAIL=1
import sys, numpy as np
src = open(sys.argv[3]).read().replace('\nmain()\n', '\n'); ns = {}; exec(src, ns)
x, sr = ns['read_wav'](sys.argv[1])
marks = {}
order = []
for line in open(sys.argv[2]):
    k, t = line.split(); marks[k] = float(t); order.append(k)
def window(k):
    i = order.index(k); return marks[k], (marks[order[i + 1]] if i + 1 < len(order) else len(x) / sr)
m = np.max(np.abs(x), axis=1)
def envelope(hop_s):
    hop = int(hop_s * sr); n = len(m) // hop
    return hop, n, 20 * np.log10(np.maximum(m[:n * hop].reshape(n, hop).max(axis=1), 1e-6))
ENV = {h: envelope(h) for h in (0.002, 0.010)}
def onsets(k, floor=-45, rise=12, hop_s=0.002):
    """Onsets: the level (max over hop_s) rises by `rise` dB over the 5 hops before it."""
    hop, n, env = ENV[hop_s]
    a, b = window(k); i0, i1 = int(a * sr / hop), int(b * sr / hop)
    gap = int(0.05 * sr / hop)
    on = []
    for i in range(max(i0, 6), min(i1, n)):
        if env[i] > floor and env[i] - env[i - 5:i].max() > rise and (not on or i - on[-1] > gap):
            on.append(i)
    return [(i * hop / sr, env[i:i + int(0.1 * sr / hop)].max()) for i in on]
def f0(t, dur=0.3):
    s = x[int((t + 0.03) * sr):int((t + 0.03 + dur) * sr)].mean(axis=1)
    s = s * np.hanning(len(s)); sp = np.abs(np.fft.rfft(s, 8 * len(s))); f = np.fft.rfftfreq(8 * len(s), 1 / sr)
    sp[f < 60] = 0; return f[np.argmax(sp)]
ok = True
def check(name, cond, info):
    global ok
    print(f"  {'ok  ' if cond else 'FAIL'}  {name}: {info}"); ok &= bool(cond)

lv = onsets('levels')
pk = [p for _, p in lv]
if len(pk) == 5:
    ref127, soft, full, half, ref64 = pk
    check('FULL LEVEL: soft hit (vel 30) plays at velocity 127', abs(full - ref127) < 1.0 and soft < ref127 - 6,
          f'vel127 {ref127:.1f}, vel30 {soft:.1f}, vel30+FULL {full:.1f} dBFS')
    check('HALF LEVEL: soft hit plays at velocity 64', abs(half - ref64) < 1.0 and half < ref127 - 3,
          f'vel30+HALF {half:.1f}, vel64 {ref64:.1f} dBFS')
else:
    check('FULL / HALF LEVEL hits', False, f'{len(pk)} onsets, expected 5: {pk}')

sx = onsets('sixteen', floor=-60)
pk = [p for _, p in sx][1:]
check('16 LEVELS: 16 increasing levels', len(pk) == 16 and all(b > a for a, b in zip(pk, pk[1:])),
      ' '.join(f'{p:.1f}' for p in pk) + ' dBFS')

for k, label in (('repeat', 'NOTE REPEAT held'), ('latch', 'LATCH')):
    on = [t for t, _ in onsets(k)]
    d = np.diff(on) if len(on) > 1 else np.array([0])
    med = float(np.median(d)) * 1000
    check(f'{label}: pad retriggers at 1/16 of 120 BPM (125 ms)', len(on) >= 6 and abs(med - 125) < 12,
          f'{len(on)} onsets, median interval {med:.1f} ms')
on = onsets('plain')
check('no repeat without NOTE REPEAT / LATCH', len(on) == 1, f'{len(on)} onset(s) for a 1 s hold')

nt = onsets('notes', floor=-50, rise=6, hop_s=0.010)   # the keys ring on into the next note
if len(nt) >= 3:
    f = [f0(t) for t, _ in nt[:3]]
    check('NOTES: pads play chromatic notes (pad 2 = +1 semitone, pad 13 = +1 octave)',
          abs(f[1] / f[0] - 2 ** (1 / 12)) < 0.02 and abs(f[2] / f[0] - 2) < 0.06,
          f'pad 1 {f[0]:.1f} Hz, pad 2 {f[1]:.1f} Hz, pad 13 {f[2]:.1f} Hz')
else:
    check('NOTES: pads play', False, f'{len(nt)} onsets')

def hf_db(k, cut=6000.0):
    """Energy above `cut` Hz (dB) in the window. The closed hats carry about 60 % of it on the
    Drums track (the snare and claps the rest), so erasing pad 3 lowers it by 4-5 dB. Onset counts
    are not reliable here: the hats land on the kick and snare hits and merge into their onsets."""
    a, b = window(k); seg = x[int(a * sr):int(b * sr)].mean(axis=1)
    if len(seg) < 1024: return -200.0
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / sr)
    return 10 * np.log10(max(spec[f >= cut].sum(), 1e-20))
def played(k, note):  # Drums track only
    """Notes the sequencer played for `note` between the log markers (KICK_SIM_LOG_NOTES)."""
    i = order.index(k); end = order[i + 1] if i + 1 < len(order) else None
    inside, count = False, 0
    for line in open(sys.argv[4], errors='replace'):
        if line.startswith('SimControl: echo MARK '):
            name = line.split()[-1]
            if name == k: inside = True
            elif inside and name == end: break
        elif inside and line.startswith("SimNote: track 'Drums'") and f' note {note} ' in line:
            count += 1
    return count
n1, n2, n3 = (len(onsets(k)) for k in ('play1', 'play2', 'play3'))
h1, h2 = hf_db('play1'), hf_db('play2')
p1, p2 = played('play1', 2), played('play2', 2)
check('ERASE + pad removes that pad\'s hits from playback', p1 > 0 and p2 == 0 and h2 < h1 - 3 and n3 == 0,
      f'pad 3 (hat, note 2) played {p1} times before, {p2} after ERASE + Pad 3; energy above 6 kHz '
      f'{h1:.1f} -> {h2:.1f} dB; onsets {n1} before, {n3} after ERASE + all pads')
sys.exit(0 if ok else 1)
EOF

echo "sim_pads: packets and LEDs (log $LOG)"
check "banks B-D: bank LED and pad colours per bank" afterhas banks "LED cmd=0 data=3,1"
check "banks E-H: bank LED (SHIFT + A-D)" afterhas banks "LED cmd=0 data=7,1"
check "bank B recolours the pads (keys: colour 6)" afterhas banks "PAD cmd=6 data=6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6"
check "FULL LEVEL LED on, HALF LEVEL off" afterhas levels "LED cmd=0 data=48,1"
check "HALF LEVEL LED on, FULL LEVEL off" afterhas levels "LED cmd=0 data=49,1"
check "16 LEVELS LED and level colours on the pads" afterhas sixteen "PAD cmd=10 data=1,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,7,15,23"
check "NOTE REPEAT LED" afterhas repeat "LED cmd=0 data=24,1"
check "LATCH LED" afterhas latch "LED cmd=0 data=25,1"
check "touched control takes focus" afterhas focus "SimControl: focus LEVEL"
check "slider, + / - and jog reach the focused control" sh -c "awk '/MARK focus/{f=1} /MARK qlinks/{f=0} f' '$LOG' | grep -c 'is Navigation message' | awk '\$1 >= 7 {exit 0} {exit 1}'"
check "P1-P4 reach the screen (MAIN, TRACK MIX, STEP SEQ)" afterhas qlinks "Data\[0\] From DCL:3 cmd From DCL : 1 type From DCL : 4 In Recent Prj register handle command"
check "STEP SEQ: P2 changes step 2's level on the pads" afterhas qlinks "PAD cmd=10"
check "BACK runs the header back arrow of BROWSE and PROG EDIT" sh -c "awk '/MARK back/{f=1} f' '$LOG' | grep -q 'BACK: header back'"
check "MENU: jog 2, + 1, PUSH opens the 4th item (NEXT SEQ LED)" afterhas menu "LED cmd=0 data=16,1"
check "ERASE + pad erases notes" afterhas erase "PadModifiers: ERASE pad 3 note 2: [1-9][0-9]* notes erased from track 1"
echo "sim_pads: screenshots in $OUT"
[ "$FAIL" = 0 ] && echo "sim_pads: PASS" || echo "sim_pads: FAIL"
exit $FAIL
