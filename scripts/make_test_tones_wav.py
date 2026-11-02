#!/usr/bin/env python3
"""A stereo 88.2 kHz test WAV of pure tones, the same shape as the libvgm oracle, for checking a sample path.

Pure tones have no codec noise to hide behind: after the drift is taken out, what is left against the exact decode
is the path's own error at each frequency. The tones are 300 Hz, 1.2 kHz, 4.8 kHz, 9.6 kHz and 16.8 kHz, equal in
both channels, one after another in a 0.5 s window each so the stream's behaviour is visible per frequency.

    python3 scripts/make_test_tones_wav.py --out assets/imported/ima-proto/tones/tones_88200.wav --seconds 12
"""
import argparse
import wave
from pathlib import Path

import numpy as np

RATE = 88200
TONES = (300.0, 1200.0, 4800.0, 9600.0, 16800.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=12.0)
    args = parser.parse_args()

    n = int(args.seconds * RATE)
    t = np.arange(n) / RATE
    x = np.zeros(n)
    slot = len(TONES)
    for i, f in enumerate(TONES):
        window = (t >= args.seconds * i / slot) & (t < args.seconds * (i + 1) / slot)
        x[window] = 8000 * np.sin(2 * np.pi * f * t[window])
    stereo = np.stack([x, x], axis=1)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(args.out), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(np.round(stereo).astype('<i2').tobytes())
    print(f'{args.out}: {n} frames, tones {TONES} Hz, {args.seconds} s')


if __name__ == '__main__':
    main()
