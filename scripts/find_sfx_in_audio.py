#!/usr/bin/env python3
"""Find where extracted sound effects are heard in an emulator's audio dump.

Reads a raw dump of signed 16-bit stereo samples (the Ymir probe's `--dump-audio`, 44,100 Hz) and the sound file the
extractor wrote (SFX.BIN, FSFX.BIN or BSFX.BIN with its `tables/psx_sfx.tsv`). Each named sound is resampled to the dump's
rate and slid over the mono mix; the times where its normalised correlation passes the threshold are printed. Music or
other sounds playing at the same time lower the score, so a hit is evidence the sample sounded, and a miss is not proof
that it did not.

    scripts/find_sfx_in_audio.py build/x/audio.raw build/saturn/generated/ball_sfx/BSFX.BIN --names ball_serve ball_bounce
"""
import argparse
from pathlib import Path

import numpy as np


def load_table(sfx):
    rows = {}
    for line in (sfx.parent / 'tables/psx_sfx.tsv').read_text().splitlines():
        f = line.split('\t')
        rows[f[0]] = (int(f[3]), int(f[4]), int(f[5]))      # rate, offset, samples
    return rows


def hits(mix, pcm, threshold, gap):
    """Times (in dump samples) where the template's normalised correlation with the mix passes the threshold."""
    n = len(pcm)
    t = pcm - pcm.mean()
    t /= np.linalg.norm(t) or 1.0
    size = 1 << int(np.ceil(np.log2(len(mix) + n)))
    corr = np.fft.irfft(np.fft.rfft(mix, size) * np.conj(np.fft.rfft(t, size)), size)[:len(mix) - n + 1]
    c = np.concatenate(([0.0], np.cumsum(mix))); c2 = np.concatenate(([0.0], np.cumsum(mix * mix)))
    s, s2 = c[n:] - c[:-n], c2[n:] - c2[:-n]
    energy = np.sqrt(np.maximum(s2 - s * s / n, 1e-9))
    score = corr / energy
    found, last = [], -gap
    for i in np.flatnonzero(score > threshold):
        if i - last < gap:
            if score[i] > found[-1][1]: found[-1] = (i, score[i])
            continue
        found.append((i, score[i])); last = i
    return found


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('audio', type=Path)
    p.add_argument('sfx', type=Path)
    p.add_argument('--names', nargs='+', required=True)
    p.add_argument('--rate', type=int, default=44100)
    p.add_argument('--threshold', type=float, default=0.5)
    p.add_argument('--fields-per-second', type=float, default=59.94, help='to print the probe field of each hit')
    a = p.parse_args()
    raw = np.fromfile(a.audio, dtype='<i2').astype(np.float64)
    mix = raw[0::2] + raw[1::2]
    table, blob = load_table(a.sfx), np.frombuffer(a.sfx.read_bytes(), dtype=np.int8)
    for name in a.names:
        rate, offset, samples = table[name]
        src = blob[offset:offset + samples].astype(np.float64)
        pcm = np.interp(np.arange(0, samples * a.rate / rate) * rate / a.rate, np.arange(samples), src)
        found = hits(mix, pcm, a.threshold, len(pcm))
        times = ', '.join(f'{i / a.rate:.2f}s (field {i / a.rate * a.fields_per_second:.0f}, {s:.2f})' for i, s in found)
        print(f'{name}: {len(found)} hit(s){": " + times if found else ""}')


if __name__ == '__main__':
    main()
