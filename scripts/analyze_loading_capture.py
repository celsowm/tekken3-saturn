#!/usr/bin/env python3
"""Measure plain-background fields in a selected loading-screen video window."""
import argparse
import json
import subprocess
from pathlib import Path


def analyze(movie, start, seconds, rate=60):
    if start < 0 or seconds <= 0 or rate <= 0:
        raise ValueError('start, duration or frame rate is invalid')
    # Nearest-neighbor scaling keeps an exact plain background plain. Count
    # distinct RGB values, rather than treating any particular color as blank.
    command = ['ffmpeg', '-v', 'error', '-ss', str(start), '-i', str(movie),
               '-t', str(seconds), '-an', '-vf', f'fps={rate},scale=64:48:flags=neighbor',
               '-pix_fmt', 'rgb24', '-f', 'rawvideo', 'pipe:1']
    raw = subprocess.check_output(command)
    size = 64 * 48 * 3
    if len(raw) % size or not raw:
        raise ValueError('capture did not produce complete frames')
    blank = []
    for frame in range(len(raw) // size):
        pixels = raw[frame*size:(frame+1)*size]
        colors = {pixels[i:i+3] for i in range(0, size, 3)}
        if len(colors) <= 2:
            blank.append(frame)
    return dict(start_seconds=start, duration_seconds=seconds, sample_rate=rate,
                frames=len(raw)//size, plain_background_frames=len(blank),
                plain_background_indices=blank,
                scope='Counts nearly uniform sampled frames; inspect screenshots to identify their cause.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--start', type=float, required=True)
    parser.add_argument('--seconds', type=float, required=True)
    parser.add_argument('--rate', type=int, default=60)
    args = parser.parse_args()
    try:
        print(json.dumps(analyze(args.capture, args.start, args.seconds, args.rate), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
