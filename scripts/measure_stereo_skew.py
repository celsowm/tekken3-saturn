#!/usr/bin/env python3
"""Whole-sample delay of the right channel against the left in a stereo capture, from the capture alone.

The two channels of a stereo music track carry the same programme material in varying amounts, so the
correlation of L with R shifted by k peaks at the skew the playback path adds (the oracle is not needed).

    python3 scripts/measure_stereo_skew.py assets/imported/ima-proto/run/audio.s16le --seconds 60 --max-lag 16
"""
import argparse
from pathlib import Path

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('capture', type=Path, help='44100 Hz stereo s16le')
    parser.add_argument('--seconds', type=float, default=60.0, help='analysed window, from the first sound')
    parser.add_argument('--skip', type=float, default=0.0, help='seconds to skip after the first sound')
    parser.add_argument('--max-lag', type=int, default=16)
    args = parser.parse_args()

    x = np.frombuffer(args.capture.read_bytes(), dtype='<i2').reshape(-1, 2).astype(np.float64)
    start = int(np.argmax(np.abs(x).max(axis=1) > 200)) + int(args.skip * 44100)
    x = x[start:start + int(args.seconds * 44100)]
    left, right = x[:, 0], x[:, 1]
    best = None
    for k in range(-args.max_lag, args.max_lag + 1):
        a, b = (left[:len(left) - k], right[k:]) if k >= 0 else (left[-k:], right[:len(right) + k])
        c = float(np.dot(a, b) / max(np.sqrt(np.dot(a, a) * np.dot(b, b)), 1e-9))
        if best is None or c > best[1]:
            best = (k, c)
    print(f'right is {best[0]} samples late (corr {best[1]:.4f}) over {len(x) / 44100:.0f} s')


if __name__ == '__main__':
    main()
