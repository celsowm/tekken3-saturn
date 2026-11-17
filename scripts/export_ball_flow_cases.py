#!/usr/bin/env python3
"""Export golden cases of Tekken Ball's opponent choice for tests/test_ball_flow.c.

Runs the pinned Tekken3Remake's verified port (`screens_sim.ball_opponent`, FUN_800B528C) on random states, one
choice per case. Per case (big-endian int32): unlocked mask, generator, frame counter, first-game flag, 22 met
counts, then the choice key, the generator, the first-game flag and the 22 met counts afterwards. Run it with the
Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_flow_cases.py \
        --out build/ball-cases/flow.bin --cases 4000
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import screens_sim as ss         # noqa: E402
from fight_sim import Ram       # noqa: E402

MAGIC = 0x54334246               # 'T3BF'
CTX = 0x80001000
S2 = 0x800AE508                  # the caller's $s2 in the game
FRAME = 0x800AE6E0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=4000)
    parser.add_argument('--seed', type=int, default=11)
    args = parser.parse_args()
    rnd = random.Random(args.seed)
    out = bytearray(struct.pack('>3I', MAGIC, 1, args.cases))
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        mask = rnd.getrandbits(22) | rnd.choice([0x1, 0x2, 0x8, 0x40])
        spread = rnd.choice([3, 8, 30])
        met = [rnd.randint(0, spread) for _ in range(22)]
        lcg, frame, first = rnd.getrandbits(32), rnd.getrandbits(16), rnd.randint(0, 1)
        ram.put(ss.UNLOCKED, 'I', mask)
        ram.put(ss.LCG2, 'I', lcg)
        ram.put(FRAME, 'I', frame)
        ram.put(ss.BALL_FIRST_DONE, 'B', first)
        for c, m in enumerate(met):
            ram.put(CTX + 0x44 + 2 * c, 'H', m)
        ss.ball_opponent(ram, CTX, S2)
        after = [ram.u16(CTX + 0x44 + 2 * c) for c in range(22)]
        out += struct.pack('>26I', mask, lcg, frame, first, *met)
        out += struct.pack('>25I', ram.u32(CTX + 0x70), ram.u32(ss.LCG2), ram.u8(ss.BALL_FIRST_DONE), *after[:22])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
