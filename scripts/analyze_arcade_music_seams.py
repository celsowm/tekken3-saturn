#!/usr/bin/env python3
"""Loop seams of an Arcade song played by the SCSP, against the libvgm oracle.

Takes the two aligned WAVs scripts/compare_c352_scsp.py writes (reference_44100.wav and scsp_aligned.wav) and, for
the intro-to-loop seam and each loop-to-loop seam, measures a window around the seam and compares it with control
windows of the same length elsewhere in the song:
  - envelope correlation (10 ms) and spectrogram cosine, SCSP against the oracle;
  - dropouts: 50 ms blocks where the SCSP is under a quarter of the oracle's level while the oracle is audible;
  - level steps: the largest jump between consecutive 10 ms SCSP levels (dB), against the oracle's;
  - loop repeat: the SCSP's second pass of the loop against its first, and the oracle's against its own.
Prints JSON.

    python3 scripts/analyze_arcade_music_seams.py DIR/compare --intro-ms 54806 --loop-ms 75631"""
import argparse
import json
import wave
from pathlib import Path

import numpy as np

RATE = 44100


def load(path):
    with wave.open(str(path)) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels()).astype(np.float64)
    return x.mean(axis=1)


def env(x, hop=441):
    n = len(x) // hop
    return np.sqrt((x[:n * hop].reshape(n, hop) ** 2).mean(axis=1) + 1e-9)


def corr(a, b):
    a, b = a - a.mean(), b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d else 0.0


def spec_cos(a, b, size=2048):
    n = min(len(a), len(b)) // size
    if n == 0:
        return 0.0
    win = np.hanning(size)
    fa = np.abs(np.fft.rfft(a[:n * size].reshape(n, size) * win, axis=1))
    fb = np.abs(np.fft.rfft(b[:n * size].reshape(n, size) * win, axis=1))
    lo, hi = int(100 * size / RATE), int(8000 * size / RATE)
    fa, fb = fa[:, lo:hi], fb[:, lo:hi]
    num = (fa * fb).sum(axis=1)
    den = np.sqrt((fa * fa).sum(axis=1) * (fb * fb).sum(axis=1)) + 1e-9
    return float((num / den).mean())


def window(ref, cap, t0, t1):
    a, b = int(t0 * RATE), int(t1 * RATE)
    r, c = ref[a:b], cap[a:b]
    er, ec = env(r), env(c)
    audible = er > 0.05 * er.max() if len(er) else er
    step = lambda e: float(np.abs(np.diff(20 * np.log10(e + 1e-3))).max()) if len(e) > 1 else 0.0
    blocks = 5
    n = len(er) // blocks
    low = 0
    for i in range(n):
        sr, sc = er[i * blocks:(i + 1) * blocks].mean(), ec[i * blocks:(i + 1) * blocks].mean()
        if sr > 0.1 * er.max() and sc < 0.25 * sr:
            low += 1
    return dict(from_s=round(t0, 3), to_s=round(t1, 3), envelope_corr=round(corr(er, ec), 3),
                spectrogram_cosine=round(spec_cos(r, c), 3), dropout_blocks_50ms=low,
                max_level_step_db_scsp=round(step(ec), 1), max_level_step_db_oracle=round(step(er), 1),
                rms_ratio_db=round(float(20 * np.log10((np.sqrt((c ** 2).mean()) + 1e-9) / (np.sqrt((r ** 2).mean()) + 1e-9))), 2),
                audible_fraction=round(float(audible.mean()) if len(er) else 0.0, 2))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('compare_dir', type=Path)
    p.add_argument('--intro-ms', type=float, required=True)
    p.add_argument('--loop-ms', type=float, required=True)
    p.add_argument('--half-window', type=float, default=1.0, help='seconds each side of a seam')
    args = p.parse_args()
    ref, cap = load(args.compare_dir / 'reference_44100.wav'), load(args.compare_dir / 'scsp_aligned.wav')
    total = min(len(ref), len(cap)) / RATE
    h = args.half_window
    seams = []
    t = args.intro_ms / 1000
    while t + h < total:
        seams.append(t)
        t += args.loop_ms / 1000
    out = dict(seams=[], controls=[])
    for k, t in enumerate(seams):
        w = window(ref, cap, t - h, t + h)
        w['kind'] = 'intro_to_loop' if k == 0 else 'loop_to_loop'
        w['seam_s'] = round(t, 3)
        out['seams'].append(w)
    controls = [c for c in np.arange(5.0, total - h, 7.3) if all(abs(c - s) > 2 * h for s in seams)]
    out['controls'] = [window(ref, cap, c - h, c + h) for c in controls]
    for key in ('envelope_corr', 'spectrogram_cosine', 'rms_ratio_db'):
        v = np.array([c[key] for c in out['controls']])
        out['controls_' + key] = dict(mean=round(float(v.mean()), 3), min=round(float(v.min()), 3), max=round(float(v.max()), 3))
    out['controls_dropout_blocks_total'] = int(sum(c['dropout_blocks_50ms'] for c in out['controls']))
    out['controls_count'] = len(out['controls'])
    del out['controls']
    # the loop played twice: pass 2 against pass 1, for the capture and for the oracle
    a, b = int(args.intro_ms / 1000 * RATE), int((args.intro_ms + args.loop_ms) / 1000 * RATE)
    n = min(b - a, len(cap) - b)
    if n > 0:
        # the capture and the oracle both start the second pass with the first pass's tails still sounding; skip 3 s
        s = int(3 * RATE)
        out['loop_repeat'] = dict(
            scsp_pass2_vs_pass1_envelope_corr=round(corr(env(cap[a + s:a + n]), env(cap[b + s:b + n])), 3),
            oracle_pass2_vs_pass1_envelope_corr=round(corr(env(ref[a + s:a + n]), env(ref[b + s:b + n])), 3),
            scsp_pass2_vs_pass1_spectrogram_cosine=round(spec_cos(cap[a + s:a + n], cap[b + s:b + n]), 3),
            oracle_pass2_vs_pass1_spectrogram_cosine=round(spec_cos(ref[a + s:a + n], ref[b + s:b + n]), 3))
    print(json.dumps(out, indent=2))


if __name__ == '__main__':
    main()
