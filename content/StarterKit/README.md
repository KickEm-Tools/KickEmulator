# Starter Kit

The emulator's own demo content, because the factory sample library is not part of the firmware
release. The Kit has 16 drum sounds, two tonal instruments, two loops, a chord stab and a riser, a metronome click set, and a
"Starter Kit" project that uses them on three tracks with two sequences. All of it is synthesised by
`make_starter_kit.py` from fixed seeds, using only the Python standard library. It is dedicated
to the public domain under **CC0 1.0** (`LICENSE`).

| Here | Installed into the firmware's data paths as (phase 5 installer) |
|---|---|
| `samples/` (`Kit`, `Instruments`, `Loops`, `Metronome`) | `assets/StarterKit/` and `data/samplesKick/StarterKit/` |
| `demos/Starter Kit/` (`.kickPrj`, 120x70 `.png`) | `demos/Starter Kit/` |
| `recent_project/Starter Kit.png` (180x130 tile) | `assets/Default/recent_project/Starter Kit.png` |

The project names its samples relative to the samples root (`StarterKit/Kit/01 Kick.wav`). It
loads only on a firmware that resolves relative sample paths (`fix/relative-sample-paths`). The
metronome falls back to `assets/StarterKit/Metronome/Click.wav` with a bar accent only on a
firmware with `feature/metronome`. See `docs/STANDALONE_SPLIT.md` on the firmware work branch.

## Regenerating

```sh
python3 content/StarterKit/make_starter_kit.py             # rewrites the files here
python3 content/StarterKit/make_starter_kit.py --out DIR   # writes the same tree under DIR
```

The output is byte-identical to the committed files. This was checked on 2026-10-28 with Python
3.12.9 on macOS: all 27 files had the same SHA-256 as the copies on
`rbdone/goal-simulator-controls` at `6b6024ce`. The WAVs depend only on Python's `random` and
`math`. The PNGs go through `zlib.compress`, so a different zlib build (zlib-ng, for example)
could produce different PNG bytes that decode to the same pixels.
