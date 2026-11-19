#!/usr/bin/env python3
"""Deterministic two-pad combat device script (movement, strings, blocks) for matched A/B runs.

Format is the Ymir probe device script: `<field> <port> buttons=<A+B>`. The
tape is a pure function of --seed and --fields, so control and candidate share
byte-identical input. It carries no game state.
"""
import argparse
from pathlib import Path

ATTACKS = ('X', 'Y', 'A', 'B', 'X+Y', 'DOWN+A', 'RIGHT+X', 'LEFT+Y', 'DOWN+RIGHT+X', 'A+B')
MOVES = ('RIGHT', 'LEFT', 'DOWN', 'UP', 'RIGHT+DOWN', 'LEFT+DOWN')


def lcg(seed):
    while True:
        seed = (seed * 1664525 + 1013904223) & 0xffffffff
        yield seed >> 8


def build(fields, seed, start):
    rng = lcg(seed)
    lines = ['0 1 buttons=NONE', '0 2 buttons=NONE']
    # Both pads close the gap first, then alternate pressure and defence.
    lines += [f'{start} 1 buttons=RIGHT', f'{start} 2 buttons=LEFT',
              f'{start + 80} 1 buttons=NONE', f'{start + 80} 2 buttons=NONE']
    tick = start + 100
    while tick < fields - 40:
        for port in (1, 2):
            roll = next(rng) % 10
            if roll < 5:
                pad = ATTACKS[next(rng) % len(ATTACKS)]
            elif roll < 8:
                pad = 'LEFT' if port == 1 else 'RIGHT'   # hold back to block
            else:
                pad = MOVES[next(rng) % len(MOVES)]
            lines.append(f'{tick + (port - 1) * 3} {port} buttons={pad}')
            lines.append(f'{tick + (port - 1) * 3 + 6 + next(rng) % 12} {port} buttons=NONE')
        tick += 24 + next(rng) % 24
    lines.sort(key=lambda l: (int(l.split()[0]), int(l.split()[1])))
    # Strictly one entry per (field, port): a later release at the same field wins.
    seen = {}
    for line in lines:
        f, p = line.split()[:2]
        seen[(int(f), int(p))] = line
    return '\n'.join(seen[k] for k in sorted(seen)) + '\n'


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dest', type=Path)
    p.add_argument('--fields', type=int, default=62000)
    p.add_argument('--seed', type=int, default=3)
    p.add_argument('--start', type=int, default=600, help='first field after the round intro')
    a = p.parse_args()
    a.dest.parent.mkdir(parents=True, exist_ok=True)
    a.dest.write_text(build(a.fields, a.seed, a.start))
