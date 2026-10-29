#!/usr/bin/env python3
"""Compare global and per-quarter additive floor shading in a private stage.

This palette-weighted model is not a raster or performance measurement.
Each entry of the original fade table is weighted equally, not by its
frequency in a camera path.
"""
import argparse
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    args = parser.parse_args()
    data = args.stage.read_bytes()
    if len(data) < 5920 or data[:8] != b'T3ST\x00\x00\x00\x02' or data[56:60] != b'FADE':
        raise ValueError('expected version 2 shaded PAUL.STG')
    palette = struct.unpack_from('>16H', data, 4096)
    tile, fade, corrections = data[2048:4096], data[4128:5152], data[5152:5917]
    counts = [[0] * 16 for _ in range(4)]
    for y in range(64):
        for x in range(64):
            # PAUL.STG stores 8x8 cells consecutively, not linear rows.
            byte = tile[((y // 8) * 8 + x // 8) * 32 + (y % 8) * 4 + (x % 8) // 2]
            counts[(y // 32) * 2 + x // 32][byte & 15 if x & 1 else byte >> 4] += 1
    total = optimal = 0
    for value in fade:
        level = 128 - ((value * 51 + 127) >> 7)
        for channel, shift in enumerate((0, 5, 10)):
            colours = [(c >> shift) & 31 for c in palette]
            targets = [min(31, int(c * level / 128 + 0.5)) for c in colours]

            def error(hist, delta):
                return sum(n * abs(max(0, min(31, c + delta)) - target)
                           for n, c, target in zip(hist, colours, targets))

            delta = struct.unpack('b', corrections[level * 3 + channel:level * 3 + channel + 1])[0]
            total += sum(error(hist, delta) for hist in counts)
            optimal += sum(min(error(hist, d) for d in range(-16, 16)) for hist in counts)
    samples = len(fade) * 64 * 64 * 3
    print(json.dumps({'fade_entries': len(fade), 'distinct_levels': len({128 - ((v * 51 + 127) >> 7) for v in fade}),
                      'global_error_5bit': round(total / samples, 6),
                      'quarter_error_5bit': round(optimal / samples, 6),
                      'improvement_5bit': round((total - optimal) / samples, 6),
                      'model': 'equal weight per fade-table entry and tile texel; independent quarter offsets'}, indent=2))


if __name__ == '__main__':
    main()
