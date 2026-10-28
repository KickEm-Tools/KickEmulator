#!/usr/bin/env python3
"""wav_measure.py FILE [--onsets [--freqs]] [--channels] [--from S] [--to S] [--thr DB]

Reads a PCM16/24/32 or float32 WAV (no scipy), prints length, peak, RMS, and optionally
the onset times of bursts (first sample whose |x| crosses the threshold after >= 20 ms
of silence). Used to measure the audio probe (KICK_SIM_AUDIO_PROBE) and recorded samples;
needs numpy."""
import struct, sys
import numpy as np


def read_wav(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', 'not a WAV'
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            tag, ch, sr, _, _, bits = struct.unpack('<HHIIHH', body[:16])
            if tag == 0xFFFE:
                tag = struct.unpack('<H', body[24:26])[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b'data':
            raw = body
        pos += 8 + size + (size & 1)
    tag, ch, sr, bits = fmt
    if tag == 3:
        x = np.frombuffer(raw, dtype='<f4' if bits == 32 else '<f8')
    elif bits == 16:
        x = np.frombuffer(raw, dtype='<i2') / 32768.0
    elif bits == 24:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v / float(1 << 23)
    else:
        x = np.frombuffer(raw, dtype='<i4') / 2147483648.0
    x = x[: len(x) // ch * ch].reshape(-1, ch)
    return x, sr


def db(v):
    return 20 * np.log10(max(v, 1e-6))


def main():
    a = sys.argv[1:]
    path = a[0]
    x, sr = read_wav(path)
    t0 = float(a[a.index('--from') + 1]) if '--from' in a else 0.0
    t1 = float(a[a.index('--to') + 1]) if '--to' in a else len(x) / sr
    thr = 10 ** (float(a[a.index('--thr') + 1]) / 20) if '--thr' in a else 10 ** (-30 / 20)
    seg = x[int(t0 * sr):int(t1 * sr)]
    mono = np.max(np.abs(seg), axis=1) if len(seg) else np.zeros(0)
    print(f'{path}: {x.shape[1]} ch, {sr} Hz, {len(x)} frames = {len(x) / sr:.4f} s')
    if len(seg):
        print(f'  [{t0:.3f}s, {t1:.3f}s]: peak {db(np.max(mono)):.2f} dBFS, rms {db(np.sqrt(np.mean(seg ** 2))):.2f} dBFS')
        nz = np.nonzero(mono > thr)[0]
        if len(nz):
            print(f'  above {db(thr):.0f} dBFS from {t0 + nz[0] / sr:.4f}s to {t0 + nz[-1] / sr:.4f}s')
        if '--channels' in a:
            for c in range(seg.shape[1]):
                print(f'  ch{c}: peak {db(np.max(np.abs(seg[:, c]))):.2f} dBFS, rms {db(np.sqrt(np.mean(seg[:, c] ** 2))):.2f} dBFS')
    if '--onsets' in a:
        quiet = int(0.02 * sr)
        on, last = [], -10 ** 9
        above = np.nonzero(mono > thr)[0]
        for i in above:
            if i - last > quiet:
                on.append(t0 + i / sr)
            last = i
        print('  onsets (s): ' + ' '.join(f'{t:.4f}' for t in on))
        if len(on) > 1:
            d = np.diff(on)
            print('  intervals (s): ' + ' '.join(f'{v:.4f}' for v in d))
        if '--freqs' in a and on:
            # Dominant frequency of the 40 ms after each onset (Hann window, zero-padded FFT).
            fs = []
            for t in on:
                i = int((t - t0) * sr)
                w = seg[i:i + int(0.04 * sr)].mean(axis=1)
                if len(w) < 64:
                    fs.append(0.0)
                    continue
                spec = np.abs(np.fft.rfft(w * np.hanning(len(w)), 8192))
                fs.append(np.argmax(spec) * sr / 8192)
            print('  onset freqs (Hz): ' + ' '.join(f'{f:.0f}' for f in fs))
            pk = []
            for t in on:
                i = int((t - t0) * sr)
                pk.append(db(np.max(np.abs(seg[i:i + int(0.04 * sr)]))))
            print('  onset peaks (dBFS): ' + ' '.join(f'{p:.1f}' for p in pk))


main()
