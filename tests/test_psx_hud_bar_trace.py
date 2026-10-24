#!/usr/bin/env python3
"""Compare C FUN_8004E55C bar steps with the pinned original-code fight traces.

The trace records original RAM after every fight frame. Each case feeds the
previous recorded HUD state and the current fighter health/max to the C step,
then checks all four resulting HUD fields. Private traces stay ignored.
"""
import argparse
import ast
import json
import struct
import subprocess
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = [ROOT / f'third_party/Tekken3Remake/work/traces/fight_{name}.bin'
           for name in ('paul_law', 'lei_king', 'long_match')]


def frames(path):
    data = path.read_bytes()
    if data[:4] != b'T3FT' or struct.unpack_from('<I', data, 4)[0] != 2:
        raise ValueError(f'{path}: unsupported fight trace')
    header_bytes = struct.unpack_from('<I', data, 8)[0]
    header = json.loads(data[12:12 + header_bytes])
    offset = 12 + header_bytes
    body_bytes, compressed_bytes = struct.unpack_from('<II', data, offset)
    body = zlib.decompress(data[offset + 8:offset + 8 + compressed_bytes])
    if len(body) != body_bytes:
        raise ValueError(f'{path}: incorrect decompressed length')
    ranges = header['ranges']
    if isinstance(ranges, str):
        ranges = ast.literal_eval(ranges)
    fighter_size = header['fighter_size']
    offset = 0
    for _ in range(header['frames']):
        _, _, _, calls = struct.unpack_from('<HHbB', body, offset)
        offset += 6
        for _ in range(calls):
            _, arguments = struct.unpack_from('<BB', body, offset)
            offset += 2 + 4 * arguments
        fighters = (body[offset:offset + fighter_size],
                    body[offset + fighter_size:offset + 2 * fighter_size])
        offset += 2 * fighter_size
        ram = {}
        for name, _, size in ranges:
            ram[name] = body[offset:offset + size]
            offset += size
        bars = [struct.unpack_from('<ihhh', ram['hud_bars'], 4 + 16 * i)
                for i in range(2)]
        health = [struct.unpack_from('<ii', fighter, 0x3F4)
                  for fighter in fighters]
        # 0x800958A8 is the fight's rounds-played word in fight_flags.
        round_number = struct.unpack_from('<i', ram['fight_flags'], 0x68)[0]
        yield round_number, bars, health
    if offset != len(body):
        raise ValueError(f'{path}: trailing trace bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--trace', type=Path, nargs='*', default=DEFAULT)
    args = parser.parse_args()
    if any(not path.is_file() for path in args.trace):
        print('private original fight traces absent; skipped')
        return 77
    rows = []
    counts = {}
    coverage = {'health_changed': 0, 'refill': 0, 'recent_closing': 0}
    for path in args.trace:
        previous = None
        count = 0
        for round_number, bars, health in frames(path):
            if previous is not None and previous[0] == round_number:
                for i in range(2):
                    # The HUD is sometimes suppressed for a replay. Its last
                    # health then does not follow the fighter; omit that frame.
                    if bars[i][0] != health[i][0]:
                        continue
                    before = previous[1][i]
                    rows.append(' '.join(map(str, (*before, *health[i], *bars[i]))))
                    coverage['health_changed'] += bars[i][0] != before[0]
                    coverage['refill'] += bars[i][2] > before[2]
                    coverage['recent_closing'] += bars[i][3] < before[3]
                    count += 1
            previous = (round_number, bars)
        counts[path.name] = count
    if not all(coverage.values()):
        raise ValueError(f'golden traces lack a required bar transition: {coverage}')
    result = subprocess.run([str(args.runner)], input='\n'.join(rows) + '\n',
                            text=True, capture_output=True, check=False)
    print(json.dumps({'cases': counts, 'coverage': coverage,
                      'runner': result.stdout.strip(),
                      'error': result.stderr.strip()}, indent=2))
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
