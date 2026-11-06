#!/usr/bin/env python3
"""Export golden cases of Tekken Force's target choice for tests/test_psx_force_target.c.

Runs the pinned Tekken3Remake's verified `force_sim.choose_target` (force.ovl FUN_800B598C) on random
states, with the fighters' relative angles given instead of computed (the match core has them), and
writes each case's inputs and expected pick and state. Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_force_target_cases.py \
        --out build/force-cases/target.bin --cases 30000

The output holds no game data, only random states and the port's answers; it stays under build/.
"""
import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'third_party/Tekken3Remake/tools/research'))

import fight_sim                 # noqa: E402
import force_sim as fs           # noqa: E402
from fight_sim import Ram       # noqa: E402

MAGIC = 0x54335447  # 'T3TG'
WORDS_IN = 26   # 22 scalars and the four quadrant order words
WORDS_OUT = 3


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=30000)
    parser.add_argument('--seed', type=int, default=1)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    sp = lambda v: v if v < 0x8000 else v - 0x10000  # noqa: E731
    player, a, b = fs.F0, fs.F1, fs.F2
    angles = {}

    def relative_angles(fp, fo):
        quadrant, rel = angles[fo.base]
        fp.ram.put(player + 0x32, 'H', quadrant)
        fp.ram.put(player + 0x30, 'H', rel)
    fight_sim.relative_angles = relative_angles
    out = bytearray(struct.pack('>4I', MAGIC, 1, WORDS_IN, args.cases))
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        wait = rng.choice([0, 0, 1, 5, 0x78, -3])
        manual = rng.choice([0, 0, 1])
        side = rng.randrange(2)
        button = rng.choice([1, 2, 4, 8])
        close_a, close_b = rng.randrange(2), rng.randrange(2)
        pad = rng.choice([0, button, rng.getrandbits(16)])
        hidden_a, hidden_b = rng.choice([0, 0, 0, 1]), rng.choice([0, 0, 0, 1])
        index_a, index_b, index_p = rng.randrange(3), rng.randrange(3), rng.randrange(3)
        pair = [rng.choice([rng.randrange(0, 0x1400), rng.randrange(-0x1400, 0x1400)]) for _ in range(5)]
        fold = lambda i: 1 << (i - 1) if i > 1 else i  # noqa: E731  (FUN_80043C88)
        if rng.random() < 0.4:
            pair[fold(index_p) + fold(index_a)] = 0x9C4 + rng.randrange(-0x50, 0x50)
        order = [rng.randrange(4) for _ in range(4)]
        qa, ra, rb = rng.randrange(4), rng.getrandbits(16), rng.getrandbits(16)
        qb = qa if rng.random() < 0.5 else rng.randrange(4)
        if rng.random() < 0.3:
            rb = (ra + rng.choice([0, 0xFFF, 0x1000, 0x1FFF, 0x2000, 0x2FFF, 0x3000, 0x3001])) & 0xFFFF
        angles[a], angles[b] = (qa, ra), (qb, rb)
        ram.put(fs.TARGET_WAIT, 'i', wait)
        ram.put(fs.MANUAL, 'I', manual)
        ram.put(fs.MANUAL_SIDE, 'I', side)
        ram.put(fs.MANUAL_BUTTON, 'I', button)
        ram.put(fs.CLOSE_A, 'I', close_a)
        ram.put(fs.CLOSE_B, 'I', close_b)
        human = rng.randrange(2)
        ram.put(fs.HUMAN_SIDE, 'H', human)
        ram.put(fs.PAD_PRESSED + (2 if human else 0), 'H', pad)
        ram.put(a + 0xC2, 'B', hidden_a)
        ram.put(b + 0xC2, 'B', hidden_b)
        ram.put(a + 0x1E, 'B', index_a)
        ram.put(b + 0x1E, 'B', index_b)
        ram.put(player + 0x1E, 'B', index_p)
        for k, v in enumerate(pair):
            ram.put(0x8009EA08 + 0x10 * k, 'i', v)
        for k, v in enumerate(order):
            ram.put(fs.QUADRANT_ORDER + 4 * k, 'i', v)
        pick = fs.choose_target(ram, player, a, b)
        out += struct.pack(f'>{WORDS_IN}i', wait, manual, side, button, close_a, close_b, sp(pad), hidden_a, hidden_b,
                           index_a, index_b, index_p, *pair, qa, sp(ra), qb, sp(rb), human, *order)
        out += struct.pack('>3i', int(pick != a), ram.s32(fs.TARGET_WAIT), ram.s32(fs.MANUAL_SIDE))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
