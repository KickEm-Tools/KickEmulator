#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Records docs/media/kickem-demo.mp4 (H.264 + AAC, with the Kick's sound) and
# docs/media/kickem-demo.gif (the README's preview, no sound): the panel in fast mode with the
# screen embedded, from boot to the Projects screen, PUSH loading the Starter Kit, PLAY, pads hit
# while it plays, TRACK MIX with a level moved and back to MAIN, then STOP.
#
#   docs/make_demo_video.sh [--ref REF] [--firmware URL|PATH] [--no-build] [--keep-frames] [OUTDIR]
#
# Default ref: main (KickEm-Tools/0700-AAM; it has the screen mirror the embedded screen needs);
# default OUTDIR (frames, probe WAV, logs): checks/out/demo-video. Needs ffmpeg with libx264
# (brew install ffmpeg). Regenerate the committed files by running it with no arguments; it takes
# about a minute after the build. It works with the Mac locked and needs no Screen Recording
# permission: the frames are rendered in-process by the panel (kickem_ctl record, the same
# rendering as panelshot), and the sound is the app's own master output (kickem run --probe),
# lined up with the frames at PLAY (see "align" below). Nothing else on the desktop is captured.
set -u
DOCS=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DOCS/.." && pwd)
CTL="$ROOT/checks/lib/kickem_ctl"
REF=main
RUN_ARGS=""
KEEP=""
while [ $# -gt 0 ]; do
    case "$1" in
        --ref) REF=$2; shift 2 ;;
        --firmware) RUN_ARGS="$RUN_ARGS --firmware $2"; shift 2 ;;
        --no-build) RUN_ARGS="$RUN_ARGS --no-build"; shift ;;
        --keep-frames) KEEP=1; shift ;;
        -h|--help) sed -n '3,19p' "$0"; exit 0 ;;
        -*) echo "make_demo_video: unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
OUT=${1:-$ROOT/checks/out/demo-video}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
RUN="$OUT/run"
FRAMES="$OUT/frames"
MEDIA="$DOCS/media"
FPS=30
export KICKEM_RUN_DIR="$RUN"
command -v ffmpeg >/dev/null || { echo "make_demo_video: needs ffmpeg (brew install ffmpeg)"; exit 1; }

cleanup() {
    "$ROOT/kickem" stop --out "$RUN" >/dev/null 2>&1
}
trap cleanup EXIT
trap 'exit 1' INT TERM

rm -rf "$RUN" "$FRAMES" "$OUT/probe.wav"
echo "== kickem run --ref $REF (log: $OUT/kickem.log)"
# shellcheck disable=SC2086
KICK_AUDIO_INPUT=silence "$ROOT/kickem" run --ref "$REF" $RUN_ARGS --screen embedded --fresh --detach \
    --probe "$OUT/probe.wav" --out "$RUN" > "$OUT/kickem.log" 2>&1 || { tail -15 "$OUT/kickem.log"; exit 1; }
[ "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("screen"))' "$RUN/state.json")" = embedded ] \
    || { echo "make_demo_video: the screen is not embedded (does $REF have KICK_SCREEN_MIRROR?)"; exit 1; }
i=0
until grep -q "^ScreenView: first frame" "$RUN/panel.log" || [ "$i" -ge 40 ]; do i=$((i + 1)); sleep 0.25; done
sleep 1.5   # the Projects screen's thumbnails

ctl() { "$CTL" "$@" >/dev/null || echo "   kickem_ctl $* failed"; }
beat() { sleep "$1"; }

echo "== recording"
ctl record START "$FRAMES" "$FPS"
beat 2.5                                         # the Projects screen, Starter Kit selected
ctl press PUSH                                   # load it
i=0
until grep -q '^Project: loaded .*/Starter Kit' "$RUN/kick.log" || [ "$i" -ge 80 ]; do i=$((i + 1)); sleep 0.25; done
beat 2
ctl press PLAY
beat 4
for p in 1 5 1 9 1 5 13 9; do                    # pads, about on the beat (120 BPM: 0.5 s)
    ctl hold "Pad $p" 0.3 110; beat 0.15
done
beat 1
ctl press "TRACK MIX"                            # the mixer: P2 is the Drums row's LEVEL
beat 2
for s in 1 2 3 4 5; do ctl turn P2 -10; beat 0.2; done   # 100 down to 50
beat 2
for s in 1 2 3 4 5; do ctl turn P2 10; beat 0.2; done    # and back
beat 1.5
ctl press MAIN
beat 2.5
for p in 2 6 2 6; do ctl hold "Pad $p" 0.3 110; beat 0.15; done
beat 2
ctl press STOP
beat 2
"$CTL" record STOP
"$ROOT/kickem" stop --out "$RUN" >/dev/null 2>&1   # finishes the WAV's header

echo "== encoding (ffmpeg $(ffmpeg -version | sed -n 's/^ffmpeg version \([^ ]*\).*/\1/p'))"
mkdir -p "$MEDIA"
# align: a concat list that shows each frame from its own time to the next one's, and the
# offset into the WAV of the first frame. The probe writes the master output as it is rendered,
# from the start of the app, so the WAV has its own clock; it is tied to the frames' wall clock
# at PLAY: nothing sounds before it (the Projects screen and the load are silent), so the first
# sound in the WAV is the first audio block after the panel sent PLAY (frames.txt logs the
# command's time). That is within a block or two (about 10-20 ms) of the truth. (Mapping the
# WAV's growing size to the wall clock instead put the sound some 60 ms early: the audio device
# renders ahead of the clock.)
OFFSET=$(python3 - "$FRAMES" "$OUT/probe.wav" <<'EOF'
import struct, sys
import numpy as np
frames, wav = sys.argv[1:3]
data = open(wav, "rb").read()   # the probe's WAV: 32-bit float, one channel per output
pos, chunks = 12, {}
while pos + 8 <= len(data):
    cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
    chunks[cid] = data[pos + 8:pos + 8 + size]
    pos += 8 + size + (size & 1)
channels, rate = struct.unpack("<HI", chunks[b"fmt "][2:8])
x = np.frombuffer(chunks[b"data"], dtype="<f4")
x = x[:len(x) // channels * channels].reshape(-1, channels)
loud = np.flatnonzero(np.abs(x).max(axis=1) > 10 ** (-50 / 20))
if not len(loud):
    sys.exit("make_demo_video: the probe WAV is silent")
onset = loud[0] / rate
rows, play = [], None
for l in open(frames + "/frames.txt"):
    f = l.split()
    if not f:
        continue
    if f[0] == "#":
        if play is None and len(f) >= 5 and f[3] == "press" and f[4].strip('"') == "PLAY":
            play = float(f[1]) / 1000.0
    else:
        rows.append((f[0], float(f[1]) / 1000.0))
if play is None:
    sys.exit("make_demo_video: no PLAY in frames.txt")
with open(frames + "/concat.txt", "w") as o:
    for k, (name, t) in enumerate(rows):
        dur = (rows[k + 1][1] - t) if k + 1 < len(rows) else 1.0 / 30
        o.write("file '%s'\nduration %.4f\n" % (name, dur))
    o.write("file '%s'\n" % rows[-1][0])
t0 = rows[0][1]
print("%.4f" % (onset - (play - t0)))
sys.stderr.write("   %d frames over %.2f s (%.2f fps); PLAY at %.3f s into the video, first sound at %.3f s into the WAV\n"
                 % (len(rows), rows[-1][1] - t0, (len(rows) - 1) / (rows[-1][1] - t0), play - t0, onset))
EOF
) || exit 1

ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i "$FRAMES/concat.txt" -ss "$OFFSET" -i "$OUT/probe.wav" \
    -map 0:v -map 1:a -vf "fps=$FPS,scale=1000:-2:flags=lanczos,format=yuv420p" \
    -c:v libx264 -preset slow -crf 20 -tune animation -c:a aac -b:a 160k -ac 2 -af "pan=stereo|c0=c0|c1=c1" \
    -shortest -movflags +faststart "$MEDIA/kickem-demo.mp4" || exit 1
ffmpeg -hide_banner -loglevel error -y -i "$MEDIA/kickem-demo.mp4" \
    -vf "fps=12,scale=800:-2:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" \
    "$MEDIA/kickem-demo.gif" || exit 1
ls -l "$MEDIA/kickem-demo.mp4" "$MEDIA/kickem-demo.gif" | awk '{ printf "   %s  %.1f MB\n", $NF, $5 / 1048576 }'
[ -n "$KEEP" ] || rm -rf "$FRAMES"
echo "done: $MEDIA/kickem-demo.mp4, $MEDIA/kickem-demo.gif (frames, WAV and logs: $OUT)"
