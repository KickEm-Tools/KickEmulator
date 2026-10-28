#!/usr/bin/env python3
"""Synthesise the Kick starter content: a drum kit, two tonal instruments, two loops, a metronome
click set, and the "Starter Kit" demo project that uses them.

Everything is generated from fixed seeds with the Python standard library only, so the output is
byte-for-byte reproducible and clearly ours to ship (CC0, see LICENSE beside this script).

    python3 content/StarterKit/make_starter_kit.py             # rewrites the files below
    python3 content/StarterKit/make_starter_kit.py --out DIR   # writes the same tree under DIR

Outputs (paths relative to content/StarterKit/, or to DIR), and where the emulator installs them
in the firmware's data paths:
    samples/Kit/NN Name.wav               16 drum sounds (pads 1-16 of bank A)
    samples/Instruments/*.wav             Keys C4 and Bass C2, played chromatically
    samples/Loops/*.wav                   one-bar loops at 120 BPM
    samples/Metronome/Click*.wav          metronome click (beat) and accent (bar)
        samples/ -> assets/StarterKit/ and data/samplesKick/StarterKit/
    demos/Starter Kit/Starter Kit.kickPrj the project
    demos/Starter Kit/Starter Kit.png     120x70 thumbnail (names the tile image)
        demos/Starter Kit/ -> demos/Starter Kit/
    recent_project/Starter Kit.png        180x130 tile the Projects screen draws
        -> assets/Default/recent_project/Starter Kit.png

All audio is 48 kHz, 16-bit, mono WAV (the engine resamples nothing at 48 kHz and duplicates mono
to both channels). Sample paths in the project are relative to the desktop samples root
(./data/samplesKick/, where the emulator installs samples/ as StarterKit/); the engine resolves
them when it loads the project (firmware change fix/relative-sample-paths).
"""

import argparse
import math
import os
import random
import struct
import wave
import zlib

SR = 48000
OUT_DIR = os.path.dirname(os.path.abspath(__file__))  # --out overrides it (see set_out_dir)
KIT_ROOT = os.path.join(OUT_DIR, "samples")
PROJECT_DIR = os.path.join(OUT_DIR, "demos", "Starter Kit")
TILE_DIR = os.path.join(OUT_DIR, "recent_project")


def set_out_dir(path):
    global OUT_DIR, KIT_ROOT, PROJECT_DIR, TILE_DIR
    OUT_DIR = os.path.abspath(path)
    KIT_ROOT = os.path.join(OUT_DIR, "samples")
    PROJECT_DIR = os.path.join(OUT_DIR, "demos", "Starter Kit")
    TILE_DIR = os.path.join(OUT_DIR, "recent_project")


SAMPLE_PREFIX = "StarterKit"  # relative to the samples root (./data/samplesKick/ on desktop)

TAU = 2.0 * math.pi


# --------------------------------------------------------------------------------------------
# Small DSP toolkit (lists of floats, -1..1)
# --------------------------------------------------------------------------------------------
def n_samples(seconds):
    return int(round(seconds * SR))


def env_exp(n, decay_s, attack_s=0.0005):
    """Exponential decay with a short linear attack (no clicks)."""
    a = max(1, n_samples(attack_s))
    k = 1.0 / max(1e-6, decay_s * SR)
    return [(i / a if i < a else 1.0) * math.exp(-k * max(0, i - a)) for i in range(n)]


def sine_sweep(n, f_start, f_end, sweep_s, phase=0.0):
    """Sine whose frequency glides exponentially from f_start to f_end over sweep_s."""
    out, ph = [], phase
    k = 1.0 / max(1e-6, sweep_s * SR)
    for i in range(n):
        f = f_end + (f_start - f_end) * math.exp(-k * i)
        ph += TAU * f / SR
        out.append(math.sin(ph))
    return out


def osc(n, freq, shape="sine", phase=0.0):
    out, ph = [], phase
    inc = freq / SR
    for _ in range(n):
        if shape == "sine":
            v = math.sin(TAU * ph)
        elif shape == "square":
            v = 1.0 if ph < 0.5 else -1.0
        elif shape == "saw":
            v = 2.0 * ph - 1.0
        else:  # triangle
            v = 4.0 * abs(ph - 0.5) - 1.0
        out.append(v)
        ph = (ph + inc) % 1.0
    return out


def noise(n, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def lowpass(x, cutoff):
    a = 1.0 - math.exp(-TAU * cutoff / SR)
    y, out = 0.0, []
    for v in x:
        y += a * (v - y)
        out.append(y)
    return out


def highpass(x, cutoff):
    lp = lowpass(x, cutoff)
    return [a - b for a, b in zip(x, lp)]


def bandpass(x, f_lo, f_hi):
    return lowpass(highpass(x, f_lo), f_hi)


def resonator(x, freq, q):
    """Two-pole resonant band-pass (RBJ constant 0 dB peak)."""
    w = TAU * freq / SR
    alpha = math.sin(w) / (2.0 * q)
    b0, b2 = alpha, -alpha
    a0, a1, a2 = 1.0 + alpha, -2.0 * math.cos(w), 1.0 - alpha
    x1 = x2 = y1 = y2 = 0.0
    out = []
    for v in x:
        y = (b0 * v + b2 * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1, y2, y1 = x1, v, y1, y
        out.append(y)
    return out


def mul(a, b):
    return [p * q for p, q in zip(a, b)]


def mix(*parts):
    """mix((gain, signal), ...) -> sum, length of the longest signal."""
    n = max(len(s) for _, s in parts)
    out = [0.0] * n
    for g, s in parts:
        for i, v in enumerate(s):
            out[i] += g * v
    return out


def soft_clip(x, drive=1.0):
    return [math.tanh(drive * v) for v in x]


def normalise(x, peak_db=-1.0):
    peak = max(1e-9, max(abs(v) for v in x))
    g = 10 ** (peak_db / 20.0) / peak
    return [v * g for v in x]


def fade_out(x, seconds=0.005):
    n = min(len(x), n_samples(seconds))
    for i in range(n):
        x[len(x) - n + i] *= 1.0 - (i + 1) / n
    return x


def finish(x, peak_db=-1.0):
    return fade_out(normalise(x, peak_db))


# --------------------------------------------------------------------------------------------
# Drum kit
# --------------------------------------------------------------------------------------------
def kick():
    n = n_samples(0.5)
    body = mul(sine_sweep(n, 160.0, 48.0, 0.045), env_exp(n, 0.16))
    click = mul(highpass(noise(n, random.Random(101)), 2000.0), env_exp(n, 0.003))
    return finish(soft_clip(mix((1.0, body), (0.35, click)), 1.6))


def snare():
    n = n_samples(0.35)
    tone = mix((0.6, sine_sweep(n, 240.0, 185.0, 0.02)), (0.3, osc(n, 330.0)))
    tone = mul(tone, env_exp(n, 0.045))
    rattle = mul(bandpass(noise(n, random.Random(102)), 1200.0, 9000.0), env_exp(n, 0.09))
    return finish(mix((0.8, tone), (1.0, rattle)))


def metal(n, rng_seed, base=320.0):
    """Six detuned square partials (the classic analogue cymbal recipe)."""
    ratios = [1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637]
    rng = random.Random(rng_seed)
    parts = [(1.0, osc(n, base * r, "square", rng.random())) for r in ratios]
    return mix(*parts)


def hat(decay, seed):
    n = n_samples(decay * 4.0 + 0.03)
    src = mix((0.7, metal(n, seed)), (0.5, noise(n, random.Random(seed + 1))))
    src = highpass(highpass(src, 7000.0), 7000.0)
    return finish(mul(src, env_exp(n, decay)), -3.0)


def clap():
    n = n_samples(0.35)
    rng = random.Random(105)
    src = bandpass(noise(n, rng), 900.0, 2600.0)
    env = [0.0] * n
    for burst in (0.0, 0.011, 0.022):
        start = n_samples(burst)
        for i in range(start, n):
            env[i] = max(env[i], math.exp(-(i - start) / (0.006 * SR)))
    tail_start = n_samples(0.03)
    for i in range(tail_start, n):
        env[i] = max(env[i], 0.6 * math.exp(-(i - tail_start) / (0.07 * SR)))
    return finish(mul(src, env))


def rim():
    n = n_samples(0.1)
    tone = mix((1.0, osc(n, 1700.0)), (0.6, osc(n, 520.0, "triangle")))
    return finish(mul(tone, env_exp(n, 0.012)))


def tom(f_start, f_end):
    n = n_samples(0.55)
    body = mul(sine_sweep(n, f_start, f_end, 0.06), env_exp(n, 0.18))
    skin = mul(bandpass(noise(n, random.Random(int(f_start))), 300.0, 3000.0), env_exp(n, 0.012))
    return finish(mix((1.0, body), (0.25, skin)))


def crash():
    n = n_samples(1.4)
    src = mix((0.6, metal(n, 110, 380.0)), (0.6, noise(n, random.Random(111))))
    src = highpass(src, 4000.0)
    return finish(mul(src, env_exp(n, 0.45, 0.002)), -2.0)


def ride():
    n = n_samples(1.0)
    shimmer = highpass(mix((0.5, metal(n, 120, 520.0)), (0.3, noise(n, random.Random(121)))), 5000.0)
    ping = mix((1.0, osc(n, 3100.0)), (0.5, osc(n, 4620.0)))
    return finish(mix((1.0, mul(shimmer, env_exp(n, 0.35))), (0.25, mul(ping, env_exp(n, 0.25)))), -3.0)


def shaker():
    n = n_samples(0.16)
    src = bandpass(noise(n, random.Random(130)), 4000.0, 12000.0)
    env = [math.sin(math.pi * min(1.0, i / n_samples(0.02)) / 2) * math.exp(-max(0, i - n_samples(0.02)) / (0.035 * SR)) for i in range(n)]
    return finish(mul(src, env), -4.0)


def cowbell():
    n = n_samples(0.35)
    src = mix((1.0, osc(n, 540.0, "square")), (1.0, osc(n, 800.0, "square")))
    src = bandpass(src, 400.0, 3500.0)
    return finish(mul(src, env_exp(n, 0.08)), -3.0)


def conga():
    n = n_samples(0.35)
    body = mul(sine_sweep(n, 260.0, 210.0, 0.02), env_exp(n, 0.09))
    slap = mul(bandpass(noise(n, random.Random(140)), 800.0, 4000.0), env_exp(n, 0.006))
    return finish(mix((1.0, body), (0.4, slap)))


def clave():
    n = n_samples(0.12)
    return finish(mul(osc(n, 2500.0), env_exp(n, 0.018)))


def zap():
    n = n_samples(0.22)
    return finish(mul(sine_sweep(n, 2400.0, 90.0, 0.04), env_exp(n, 0.07)))


KIT = [  # (file name, generator) in pad order: pad 1 is bottom-left on the unit
    ("01 Kick", kick),
    ("02 Snare", snare),
    ("03 Closed Hat", lambda: hat(0.035, 103)),
    ("04 Open Hat", lambda: hat(0.22, 104)),
    ("05 Clap", clap),
    ("06 Rim", rim),
    ("07 Low Tom", lambda: tom(120.0, 82.0)),
    ("08 Mid Tom", lambda: tom(170.0, 118.0)),
    ("09 High Tom", lambda: tom(240.0, 168.0)),
    ("10 Crash", crash),
    ("11 Ride", ride),
    ("12 Shaker", shaker),
    ("13 Cowbell", cowbell),
    ("14 Conga", conga),
    ("15 Clave", clave),
    ("16 Zap", zap),
]


# --------------------------------------------------------------------------------------------
# Tonal instruments and loops
# --------------------------------------------------------------------------------------------
def note_hz(midi):
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def keys(midi=60, seconds=1.2):
    """Electric-piano-like additive tone: decaying harmonics, slightly brighter attack."""
    n = n_samples(seconds)
    f = note_hz(midi)
    parts = []
    for h, (g, d) in enumerate([(1.0, 0.9), (0.45, 0.45), (0.25, 0.25), (0.12, 0.12), (0.08, 0.06)], start=1):
        parts.append((g, mul(osc(n, f * h * (1.0 + 0.0007 * h)), env_exp(n, d, 0.002))))
    tine = mul(osc(n, f * 7.0), env_exp(n, 0.015))
    return finish(mix(*parts, (0.15, tine)), -2.0)


def bass(midi=36, seconds=0.8):
    n = n_samples(seconds)
    f = note_hz(midi)
    raw = mix((0.7, osc(n, f, "saw")), (0.5, osc(n, f * 0.5005, "square")))
    filt = lowpass(lowpass(raw, 900.0), 900.0)
    return finish(soft_clip(mul(filt, env_exp(n, 0.35, 0.003)), 1.4), -2.0)


def place(buf, sound, at_s, gain=1.0):
    start = n_samples(at_s)
    for i, v in enumerate(sound):
        if start + i < len(buf):
            buf[start + i] += gain * v


def drum_loop(sounds):
    """One bar of 4/4 at 120 BPM (2 s), rendered from the kit."""
    step = 60.0 / 120.0 / 4.0
    buf = [0.0] * n_samples(2.0)
    for s in (0, 6, 8, 10):
        place(buf, sounds["01 Kick"], s * step, 0.9)
    for s in (4, 12):
        place(buf, sounds["02 Snare"], s * step, 0.7)
    for s in range(0, 16, 2):
        place(buf, sounds["03 Closed Hat"], s * step, 0.35 if s % 4 else 0.5)
    place(buf, sounds["04 Open Hat"], 14 * step, 0.35)
    place(buf, sounds["12 Shaker"], 3 * step, 0.3)
    place(buf, sounds["12 Shaker"], 11 * step, 0.3)
    return finish(buf, -1.5)


def bass_loop():
    step = 60.0 / 120.0 / 4.0
    buf = [0.0] * n_samples(2.0)
    for s, note, length in ((0, 36, 3), (3, 36, 1), (6, 43, 2), (8, 39, 3), (11, 41, 2), (14, 43, 2)):
        place(buf, bass(note, length * step), s * step, 0.8)
    return finish(buf, -2.0)


def chord_stab():
    n = n_samples(0.6)
    parts = [(1.0, keys(m, 0.6)) for m in (60, 63, 67, 70)]
    return finish(mul(mix(*parts), env_exp(n, 0.25)), -2.0)


def riser():
    n = n_samples(1.0)
    src = bandpass(noise(n, random.Random(150)), 1500.0, 8000.0)
    sweep = [math.sin(TAU * (200.0 * i / SR + 700.0 * (i / SR) ** 2)) for i in range(n)]  # 200 Hz -> 1.6 kHz chirp
    env = [(i / n) ** 2 for i in range(n)]
    return finish(mul(mix((0.6, src), (0.4, sweep)), env), -3.0)


def click(freq, decay):
    n = n_samples(0.06)
    return finish(mix((1.0, mul(osc(n, freq), env_exp(n, decay))), (0.3, mul(osc(n, freq * 2.0), env_exp(n, decay / 2)))), -3.0)


# --------------------------------------------------------------------------------------------
# Writers
# --------------------------------------------------------------------------------------------
def write_wav(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    frames = b"".join(struct.pack("<h", max(-32767, min(32767, int(round(v * 32767.0))))) for v in samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(frames)


def write_png(path, width, height, pixel):
    """pixel(x, y) -> (r, g, b, a); minimal RGBA PNG writer."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def thumbnail(path, w=120, h=70):
    """Dark panel, a 4x4 pad grid in the kit's colours and a kick waveform across the top.
    Drawn on a 120x70 design grid and scaled (nearest neighbour) to w x h."""
    pad_colours = [(255, 64, 64), (255, 160, 32), (250, 220, 40), (90, 220, 90),
                   (40, 200, 220), (70, 120, 255), (170, 90, 255), (255, 80, 200)]
    wave_y = [22 + int(round(11 * math.exp(-x / 14.0) * math.sin(x / 1.8))) for x in range(48)]

    def design(x, y):
        bg = (22 + y // 6, 22 + y // 6, 28 + y // 5, 255)
        # Pad grid on the right: 4x4 pads of 11 px with 3 px gaps, pad 1 bottom-left.
        gx, gy = x - 62, y - 9
        if 0 <= gx < 56 and 0 <= gy < 56 and gx % 14 < 11 and gy % 14 < 11:
            c = pad_colours[(gx // 14 + (3 - gy // 14) * 4) % len(pad_colours)]
            k = 1.0 if gx % 14 in (0, 10) or gy % 14 in (0, 10) else 0.55
            return (int(c[0] * k), int(c[1] * k), int(c[2] * k), 255)
        # Top left: a decaying kick waveform.
        if 8 <= x < 56 and y == wave_y[x - 8]:
            return (225, 225, 235, 255)
        # Bottom left: a sequencer lane of note bars.
        if 8 <= x < 56 and 40 <= y < 62:
            col = (x - 8) // 3
            hgt = [9, 3, 5, 3, 7, 3, 5, 3, 9, 3, 5, 6, 7, 3, 8, 4][col % 16]
            if (x - 8) % 3 < 2 and 61 - y < hgt * 2:
                return (90, 200, 120, 255) if col % 4 == 0 else (60, 140, 90, 255)
        return bg

    write_png(path, w, h, lambda x, y: design(x * 120 // w, y * 70 // h))


# --------------------------------------------------------------------------------------------
# Project
# --------------------------------------------------------------------------------------------
PPQN = 96                 # AudIO::MTTimeSignature::defaultPPQN
BAR = 4 * PPQN            # 4/4
STEP = PPQN // 4          # a 16th note


def xml_escape(s):
    return s.replace("&", "&amp;").replace("\"", "&quot;").replace("<", "&lt;").replace(">", "&gt;")


class Project:
    def __init__(self):
        self.files = []       # relative sample paths, in order
        self.slices = {}      # path -> [semitones of slice 0, 1, ...]
        self.programs = []    # (id, name, type, [(note, path, semi, extra_attrs)], {pad: colour id})
        self.tracks = []      # (id, name, program id, level 0-127, [(start, length, note, velocity)])
        self.extra_sequences = []  # (id, name, bars, own bpm or None for the master tempo, tracks like above)

    def file(self, rel):
        if rel not in self.files:
            self.files.append(rel)
            self.slices[rel] = [0]
        return rel

    def slice(self, rel, semi):
        """Index of the slice of `rel` tuned by `semi` semitones (created on demand).

        The drum voice ignores a pad's own semi / fine (DrumVoice only applies the program's
        pitch), but a slice's tuning is part of its resampling rate (ASoundSlice::retune), so
        pitched pads are separate slices of one file, as the sample editor would make them."""
        semis = self.slices[rel]
        if semi not in semis:
            semis.append(semi)
        return semis.index(semi)

    @staticmethod
    def render_tracks(o, tracks, bars):
        for tid, name, prog, level, notes in tracks:
            o.append('      <TRACK id="%s" name="%s" program="%s" usingSequenceLength="1" length="%d" level="%d">' % (tid, xml_escape(name), prog, bars, level))
            o.append('        <NOTELIST>')
            for i, (start, length, note, vel) in enumerate(sorted(notes), start=1):
                o.append('          <NOTE id="%d" no="%d" velocity="%d" start="%d" length="%d"/>' % (i, note, vel, start, length))
            o.append('        </NOTELIST>')
            o.append('      </TRACK>')

    def render(self):
        for _, _, _, pads, _ in self.programs:  # register every tuned slice before writing FILEMANAGER
            for _, rel, semi, _ in pads:
                self.slice(rel, semi)
        o = []
        o.append('<?xml version="1.0" encoding="UTF-8"?>')
        o.append("")
        o.append('<ROOT name="Starter Kit" manufacturer="Musictribe" version="0.1" bpm="120.0" length="4" num="4" den="4" isMasterBPM="1">')
        o.append('  <BUSES/>')
        o.append('  <FILEMANAGER id="skFileMg">')
        o.append('    <ASSETS>')
        for i, rel in enumerate(self.files, start=1):
            o.append('      <FILE id="skFile%02d" path="%s">' % (i, xml_escape(rel)))
            for k, semi in enumerate(self.slices[rel]):
                tune = ' semi="%d"' % semi if semi else ""
                o.append('        <SLICE id="skS%02d%03d"%s/>' % (i, k, tune))
            o.append('      </FILE>')
        o.append('    </ASSETS>')
        o.append('    <AUDIO/>')
        o.append('    <EXPORTS/>')
        o.append('  </FILEMANAGER>')
        o.append('  <TEMPOS>')
        o.append('    <TEMPO bpm="120.0" num="4" den="4"/>')
        o.append('  </TEMPOS>')
        o.append('  <PROGRAMS>')
        for pid, name, ptype, pads, colours in self.programs:
            o.append('    <PROGRAM id="%s" name="%s" type="%d">' % (pid, xml_escape(name), ptype))
            if colours:
                o.append('      <COLOURS %s/>' % " ".join('clr%d="%d"' % (k, colours[k]) for k in sorted(colours)))
            o.append('      <CLIPS>')
            for note, rel, semi, extra in pads:
                attrs = 'keyNote="%d" minNote="%d" maxNote="%d"' % (note, note, note)
                for k, v in extra:
                    attrs += ' %s="%s"' % (k, v)
                o.append('        <CLIP %s>' % attrs)
                o.append('          <LAYERS>')
                o.append('            <LAYER source="%s" slice="%d"/>' % (xml_escape(rel), self.slice(rel, semi)))
                for _ in range(3):
                    o.append('            <LAYER/>')
                o.append('          </LAYERS>')
                o.append('        </CLIP>')
            o.append('      </CLIPS>')
            o.append('    </PROGRAM>')
        o.append('  </PROGRAMS>')
        o.append('  <SEQUENCES>')
        o.append('    <SEQUENCE id="skSeq001" name="Sequence 01" length="4" loop="1" loopStart="0" loopEnd="4" isMasterBPM="1">')
        self.render_tracks(o, self.tracks, 4)
        o.append('    </SEQUENCE>')
        for sid, sname, bars, bpm, tracks in self.extra_sequences:
            o.append('    <SEQUENCE id="%s" name="%s" length="%d" loop="1" loopStart="0" loopEnd="%d" isMasterBPM="%d">'
                     % (sid, xml_escape(sname), bars, bars, 0 if bpm else 1))
            if bpm:  # the sequence's own tempo (MTSequence is a MTTempoMgr on its TEMPOS child)
                o.append('      <TEMPOS>')
                o.append('        <TEMPO bpm="%.1f" num="4" den="4"/>' % bpm)
                o.append('      </TEMPOS>')
            self.render_tracks(o, tracks, bars)
            o.append('    </SEQUENCE>')
        o.append('  </SEQUENCES>')
        o.append('</ROOT>')
        return "\n".join(o) + "\n"


def drum_pattern():
    """Four bars that play every one of the 16 kit pads (notes 0-15)."""
    hits = []

    def at(bar, step, note, vel=110, length=STEP):
        hits.append((bar * BAR + step * STEP, length, note, vel))

    for bar in range(4):
        for s in (0, 8):
            at(bar, s, 0, 120)                                     # kick
        for s in (4, 12):
            at(bar, s, 1, 112)                                     # snare
        if bar != 2:
            for s in range(0, 16, 2):
                if not (bar == 3 and s >= 8):
                    at(bar, s, 2, 96 if s % 4 else 110)            # closed hat
    at(0, 0, 9, 100)                                               # crash on the one
    at(0, 3, 14, 90); at(0, 10, 14, 90)                            # clave
    at(0, 7, 5, 100)                                               # rim
    at(0, 14, 3, 96)                                               # open hat
    at(1, 10, 0, 100)                                              # extra kick
    at(1, 12, 4, 110)                                              # clap
    for s in range(1, 16, 2):
        at(1, s, 11, 80)                                           # shaker
    at(1, 3, 13, 100); at(1, 11, 13, 100)                          # conga
    for s in (0, 4, 8, 12):
        at(2, s, 10, 100)                                          # ride
    at(2, 6, 12, 96); at(2, 14, 12, 96)                            # cowbell
    at(2, 7, 5, 100)                                               # rim
    at(3, 8, 8, 110); at(3, 10, 7, 110); at(3, 12, 6, 115)         # tom fill high-mid-low
    at(3, 12, 4, 110)                                              # clap
    at(3, 15, 15, 110)                                             # zap into the loop
    return hits


def keys_pattern():
    """Chords on the Starter Keys program (pad n plays C3 + n semitones, so C4 is pad 13)."""
    notes = []
    chords = [(0, 3, 7), (5, 8, 12), (3, 7, 10), (7, 10, 14)]       # Cm, Fm, Eb, Gm (pads 1..15)
    for bar, chord in enumerate(chords):
        for beat in (0, 2):
            for n in chord:
                notes.append((bar * BAR + beat * PPQN, PPQN, 12 + n, 64))
    return notes


def loop_pattern():
    """The bass loop (Starter Loops pad 2) on every bar."""
    return [(bar * BAR, BAR, 1, 100) for bar in range(4)]


def sequence_two():
    """Sequence 02 (phase 5, mode keys): a second sequence for NEXT SEQ, at its own tempo of
    100 BPM (MASTER, SHIFT + TAP TEMPO, switches it to the 120 BPM master tempo), whose drum
    track is "played live": every hit is a few ticks off the 16th grid, deterministically, so
    TC (Timing Correct) has something to correct. Two bars of a half-time groove."""
    rng = random.Random(502)
    hits = []
    for bar in range(2):
        for step in range(16):
            notes = []
            if step in (0, 10):
                notes.append((0, 118))                       # kick
            if step == 8:
                notes.append((1, 112))                       # snare on 3 (half time)
            if step % 2 == 0:
                notes.append((2, 100 if step % 4 else 108))  # closed hat
            if bar == 1 and step == 14:
                notes.append((3, 96))                        # open hat
            for note, vel in notes:
                off = rng.randint(-9, 9)                     # up to 9 of the 24 ticks of a 16th
                if bar == 0 and step == 0:
                    off = abs(off) or 5                      # nothing before the start
                hits.append((bar * BAR + step * STEP + off, STEP, note, vel))
    tracks = [
        ("skTrk101", "Drums Live", "skPrg001", 100, hits),
        ("skTrk102", "Loops", "skPrg003", 80, [(0, BAR, 0, 100)]),  # the drum loop once
    ]
    return ("skSeq002", "Sequence 02", 2, 100.0, tracks)


def build():
    sounds = {}
    for name, gen in KIT:
        sounds[name] = gen()
        write_wav(os.path.join(KIT_ROOT, "Kit", name + ".wav"), sounds[name])

    write_wav(os.path.join(KIT_ROOT, "Instruments", "Keys C4.wav"), keys(60, 1.2))
    write_wav(os.path.join(KIT_ROOT, "Instruments", "Bass C2.wav"), bass(36, 0.8))
    write_wav(os.path.join(KIT_ROOT, "Loops", "Drum Loop 120.wav"), drum_loop(sounds))
    write_wav(os.path.join(KIT_ROOT, "Loops", "Bass Loop 120.wav"), bass_loop())
    write_wav(os.path.join(KIT_ROOT, "Loops", "Chord Stab.wav"), chord_stab())
    write_wav(os.path.join(KIT_ROOT, "Loops", "Riser.wav"), riser())
    write_wav(os.path.join(KIT_ROOT, "Metronome", "Click.wav"), click(1200.0, 0.010))
    write_wav(os.path.join(KIT_ROOT, "Metronome", "Click Accent.wav"), click(2000.0, 0.014))

    p = Project()
    kit = [p.file("%s/Kit/%s.wav" % (SAMPLE_PREFIX, name)) for name, _ in KIT]
    keys_f = p.file(SAMPLE_PREFIX + "/Instruments/Keys C4.wav")
    bass_f = p.file(SAMPLE_PREFIX + "/Instruments/Bass C2.wav")
    loops = [p.file(SAMPLE_PREFIX + "/Loops/%s.wav" % n) for n in ("Drum Loop 120", "Bass Loop 120", "Chord Stab", "Riser")]

    # Program 1, "Starter Drums" (drum): bank A the kit, B keys and C bass chromatically from
    # C, D the loops and FX then the kit a fifth lower.
    drums = [(i, kit[i], 0, []) for i in range(16)]
    drums += [(16 + i, keys_f, i, []) for i in range(16)]
    drums += [(32 + i, bass_f, i, []) for i in range(16)]
    drums += [(48 + i, loops[i], 0, []) for i in range(4)]
    drums += [(52 + i, kit[i], -7, []) for i in range(12)]
    # Pad colours (contracts::DeviceControlContracts::PadColourIds) for the banks the factory
    # default table leaves dark (it only colours pads 1-16): B sky blue, C green, D pink / orange.
    SKY, GREEN, PINK, ORANGE = 6, 3, 9, 13
    drum_colours = {16 + i: SKY for i in range(16)}
    drum_colours.update({32 + i: GREEN for i in range(16)})
    drum_colours.update({48 + i: PINK if i < 4 else ORANGE for i in range(16)})
    p.programs.append(("skPrg001", "Starter Drums", 0, drums, drum_colours))

    # Program 2, "Starter Keys": the keygroup engine is a stub in this release
    # (KeygroupProgram::noteOn returns early), so this is a drum program played chromatically:
    # pad n = C3 + n semitones over banks A-C (keys, 48 notes), bass C2 + n on D.
    keys_prog = [(i, keys_f, i - 12, []) for i in range(48)]
    keys_prog += [(48 + i, bass_f, i, []) for i in range(16)]
    p.programs.append(("skPrg002", "Starter Keys", 0, keys_prog, {i: SKY if i < 48 else GREEN for i in range(64)}))

    # Program 3, "Starter Loops" (clip): pads 1-4 the loops and FX, one-shot.
    clips = [(i, loops[i], 0, [("padPlay", "1")]) for i in range(4)]
    p.programs.append(("skPrg003", "Starter Loops", 4, clips, {}))

    # Track levels (MIDI 0-127, default 100) keep the three tracks together under 0 dBFS.
    p.tracks.append(("skTrk001", "Drums", "skPrg001", 100, drum_pattern()))
    p.tracks.append(("skTrk002", "Keys", "skPrg002", 70, keys_pattern()))
    p.tracks.append(("skTrk003", "Loops", "skPrg003", 80, loop_pattern()))
    p.extra_sequences.append(sequence_two())

    os.makedirs(PROJECT_DIR, exist_ok=True)
    with open(os.path.join(PROJECT_DIR, "Starter Kit.kickPrj"), "w", newline="\n") as f:
        f.write(p.render())
    thumbnail(os.path.join(PROJECT_DIR, "Starter Kit.png"))
    # The Projects screen draws a demo's tile from the theme (assets/<theme>/recent_project/
    # <png name in the project folder>), 180x130 like the factory demos.
    # The emulator installs it as assets/Default/recent_project/Starter Kit.png.
    thumbnail(os.path.join(TILE_DIR, "Starter Kit.png"), 180, 130)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Synthesise the Kick Starter Kit (CC0).")
    ap.add_argument("--out", metavar="DIR", help="write the tree under DIR instead of beside this script")
    args = ap.parse_args()
    if args.out:
        set_out_dir(args.out)
    build()
    total = 0
    for root, _, files in os.walk(KIT_ROOT):
        for name in files:
            if name.endswith(".wav"):
                total += os.path.getsize(os.path.join(root, name))
    print("StarterKit: %.0f kB of WAV under %s" % (total / 1024.0, KIT_ROOT))
    print("Project:    %s" % os.path.join(PROJECT_DIR, "Starter Kit.kickPrj"))
