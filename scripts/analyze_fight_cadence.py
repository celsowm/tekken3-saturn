#!/usr/bin/env python3
"""Count observed image changes in a short emulator fight recording."""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess


def analyze(capture, start, seconds):
    if start < 0 or seconds <= 0:
        raise ValueError('start must be nonnegative and duration positive')
    # Normalize Mednafen's wide-pixel recording, excluding the HUD and timer.
    command = ['ffmpeg', '-v', 'error', '-ss', str(start), '-i', str(capture),
               '-t', str(seconds), '-an', '-vf', 'scale=320:224:flags=neighbor,crop=320:180:0:44',
               '-fps_mode', 'passthrough', '-f', 'framemd5', 'pipe:1']
    output = subprocess.check_output(command, text=True)
    hashes = [line.rsplit(',', 1)[-1].strip() for line in output.splitlines()
              if line and not line.startswith('#')]
    if len(hashes) < 2:
        raise ValueError('not enough decoded frames')
    changes = sum(first != second for first, second in zip(hashes, hashes[1:]))
    runs = []
    count = 1
    for first, second in zip(hashes, hashes[1:]):
        if first == second:
            count += 1
        else:
            runs.append(count)
            count = 1
    runs.append(count)
    return dict(start_seconds=start, requested_seconds=seconds, decoded_frames=len(hashes),
                image_changes=changes, changes_per_requested_second=changes/seconds,
                hold_lengths=dict(sorted(Counter(runs).items())),
                scope='Exact changes in the cropped decoded video include background and effects, not verified VDP1 swaps or hardware timing.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--start', type=float, required=True)
    parser.add_argument('--seconds', type=float, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(analyze(args.capture, args.start, args.seconds), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
