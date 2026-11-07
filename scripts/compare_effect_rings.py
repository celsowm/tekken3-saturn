#!/usr/bin/env python3
"""Compare two effect-ring dumps image by image, matched on the game tick.

The ring (T3_EFFECT_RING) keeps the worst 32 images that showed hit
particles. The records are sorted by cost, so the two dumps list the same
images in a different order; the game tick identifies an image.

Each input is a probe directory (one written by scripts/saturn_probe.py) or
the raw ring dump itself. The ring is found through symbol-dumps.json when
the input is a directory.

Fields compared: particles, visible particles, live fighter faces, the
faces the mixed OT examined and the faces in front of the farthest
particle. The sorted-entry count is expected to differ: the candidate
inserts only the live faces, the control inserts every face.
"""
import argparse
import json
from pathlib import Path

FIELDS = ('tick', 'image', 'particles', 'visible', 'faces', 'entries', 'mixed',
          'cover', 'sort', 'compose', 'prep', 'flush', 'commands', 'submitted',
          'ticks', 'projections', 'examined', 'depth', 'reserved0', 'reserved1')
FIELDS_PER_RECORD = 20
SLOTS = 32


def ring_dump(path):
    """Return the raw ring bytes of a probe directory or a ring dump."""
    path = Path(path)
    if path.is_dir():
        manifest = json.loads((path / 'symbol-dumps.json').read_text())
        for name in ('_g_t3_effect_ring', 'g_t3_effect_ring'):
            if name in manifest:
                return (path / manifest[name]['file']).read_bytes()
        raise SystemExit('no effect ring in ' + str(path))
    return path.read_bytes()


def records(path):
    raw = ring_dump(path)
    words = [int.from_bytes(raw[i:i + 4], 'big') for i in range(0, len(raw) - 3, 4)]
    count = min(SLOTS, len(words) // FIELDS_PER_RECORD)
    out = {}
    for slot in range(count):
        record = words[slot * FIELDS_PER_RECORD:(slot + 1) * FIELDS_PER_RECORD]
        if len(record) < FIELDS_PER_RECORD:
            break
        values = dict(zip(FIELDS, record))
        if values['image'] == 0 and values['visible'] == 0:
            continue
        out[values['tick']] = values
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    args = parser.parse_args()
    control = records(args.control)
    candidate = records(args.candidate)
    shared = sorted(set(control) & set(candidate))
    print(f'control images {len(control)}, candidate images {len(candidate)}, '
          f'shared ticks {len(shared)}')
    compare = ('particles', 'visible', 'faces', 'examined', 'depth')
    bad = 0
    for tick in shared:
        for field in compare:
            if control[tick][field] != candidate[tick][field]:
                print(f'tick {tick}: {field} {control[tick][field]} != '
                      f'{candidate[tick][field]}')
                bad += 1
    only_control = sorted(set(control) - set(candidate))
    only_candidate = sorted(set(candidate) - set(control))
    print(f'mismatched fields {bad}')
    if only_control:
        print(f'only in the control: ticks {only_control}')
    if only_candidate:
        print(f'only in the candidate: ticks {only_candidate}')
    if bad:
        raise SystemExit('the effect images differ')
    print('the effect images agree on every shared tick')


if __name__ == '__main__':
    main()
