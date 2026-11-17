#!/usr/bin/env python3
"""Export golden cases of the CPU opponent's Tekken Ball hooks for tests/test_ball_ai.c.

Runs the pinned Tekken3Remake's verified port (`ai_update.ball_project`, `ball_distance`, `ball_behind`, `ball_ahead`: volley.ovl
FUN_800B4958, FUN_800B4B08, FUN_800B4B64, FUN_800B4BE4) on random players and balls. Per case (big-endian int32): the ball (x, y, z),
the players' roots (x, z) twice, then the projected line (5), each player's distance to the ball, whether the ball is behind each
and whether it is ahead of each. The file begins with the executable's sine, cosine and atan tables, so it is retail-derived and
stays under build/. Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_ai_cases.py --out build/ball-cases/ai.bin
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import ai_sim as ai              # noqa: E402
import ai_update as au           # noqa: E402
import projection_sim as pj      # noqa: E402
from fight_sim import Ram       # noqa: E402

MAGIC = 0x54334149               # 'T3AI'
EXE_BASE = 0x80010000
TRIG_BYTES, ATAN_BYTES, ATAN_TABLE = 10240, 1025, 0x800210C8
BALL = 0x80180000
REC = 0x80190000


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=4000)
    parser.add_argument('--seed', type=int, default=9)
    args = parser.parse_args()
    rnd = random.Random(args.seed)
    exe = (ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin').read_bytes()[0x800:]
    trig = exe[pj.SIN_TABLE - EXE_BASE:pj.SIN_TABLE - EXE_BASE + TRIG_BYTES]
    atan = exe[ATAN_TABLE - EXE_BASE:ATAN_TABLE - EXE_BASE + ATAN_BYTES]
    ai.start_script = lambda *a, **k: None      # the back-dash's script start is the AI's
    out = bytearray(struct.pack('>3I2I', MAGIC, 1, args.cases, len(trig), len(atan)) + trig + atan)
    f = (ai.FIGHTER_BASE, ai.FIGHTER_BASE + ai.FIGHTER_STRIDE)
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        ram.data[ram._off(EXE_BASE):ram._off(EXE_BASE) + len(exe)] = exe
        x0 = rnd.choice([rnd.randint(-0x1400, 0), rnd.randint(-0x3000, 0x3000)])
        x1 = rnd.choice([rnd.randint(0, 0x1400), x0, rnd.randint(-0x3000, 0x3000)])
        z0, z1 = rnd.randint(-0x200, 0x200), rnd.randint(-0x200, 0x200)
        ball = [rnd.randint(-0x2800, 0x2800), rnd.randint(-0x1400, -0x180), rnd.randint(-0x200, 0x200)]
        ram.put(au.BALL_OBJECT, 'I', BALL)
        for i, v in enumerate(ball):
            ram.put(BALL + 0x68 + 4 * i, 'I', v)
        for n, (x, z) in enumerate(((x0, z0), (x1, z1))):
            ram.put(f[n] + au.FIGHTER_X, 'I', x)
            ram.put(f[n] + au.FIGHTER_Z, 'I', z)
            ram.put(f[n] + 0x12, 'H', n)
        au.ball_project(ram)
        line = [ram.s32(au.BALL_LINE + o) for o in (0, 4, 8, 0xC, 0x10)]
        dist, behind, ahead = [], [], []
        for n in range(2):
            ram.put(REC, 'H', n)
            dist.append(au.ball_distance(ram, f[n]))
            behind.append(int(au.ball_behind(ram, REC)))
            ahead.append(au.ball_ahead(ram, REC))
        out += struct.pack('>3i4i5i2i2i2i', *ball, x0, z0, x1, z1, *line, *dist, *behind, *ahead)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
