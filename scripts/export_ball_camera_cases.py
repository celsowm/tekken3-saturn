#!/usr/bin/env python3
"""Export golden cases of the fight camera's framing, with and without Tekken Ball's ball, for tests/test_ball_camera.c.

Runs the pinned Tekken3Remake's verified port (`camera_sim.camera_frame_fighters`, CameraFrameFighters 0x800650F8) on random
cameras: in mode 7 with a ball (the third target, FUN_800B4948) and, as controls, in other modes without one. Per case (big-endian
int32): the source (0 fight, 1 script), pitch, yaw, restart, distance, mode 7, the ball (x, y, z), the source record (x, y, z, pitch,
yaw), the two targets (x, y, z), h, the smoothing state (depth step, depth, lateral), the height term, the moved flag; then the same
outputs after: the source record, the smoothing state, the height term and the moved flag. The file begins with the executable's sine
table (10,240 bytes), so it is retail-derived and stays under build/. Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_camera_cases.py --out build/ball-cases/camera.bin
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import camera_sim as cs          # noqa: E402
import projection_sim as pj      # noqa: E402
from fight_sim import Ram       # noqa: E402

MAGIC = 0x54334341               # 'T3CA'
EXE_BASE = 0x80010000
TRIG_BYTES = 10240


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=4000)
    parser.add_argument('--seed', type=int, default=13)
    args = parser.parse_args()
    rnd = random.Random(args.seed)
    exe = (ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin').read_bytes()[0x800:]
    trig = exe[pj.SIN_TABLE - EXE_BASE:pj.SIN_TABLE - EXE_BASE + TRIG_BYTES]
    out = bytearray(struct.pack('>3I', MAGIC, 1, args.cases) + trig)
    made = 0
    while made < args.cases:
        ram = Ram(bytes(0x200000))
        ram.data[ram._off(EXE_BASE):ram._off(EXE_BASE) + len(exe)] = exe
        src = rnd.randint(0, 1)
        pitch = rnd.choice([rnd.randint(-0x8000, 0x8000), rnd.randint(-0x20000, 0x20000)])
        yaw = rnd.randint(-0x40000, 0x40000)
        reset = rnd.choice([0, 0, 0, 1])
        distance = rnd.choice([rnd.randint(0x300, 0x1400), 0x800])
        mode7 = rnd.choice([0, 1, 1, 1])
        ball = [rnd.randint(-0x2800, 0x2800), rnd.randint(-0x1400, 0), rnd.randint(-0x200, 0x200)]
        rec = [rnd.randint(-0x3000, 0x3000), rnd.randint(-0x1000, 0), rnd.randint(-0x6000, -0x1000),
               rnd.randint(-0x20000, 0x20000), rnd.randint(-0x40000, 0x40000)]
        targets = [[rnd.randint(-0x2800, 0x2800), rnd.randint(-0x1000, -0x100), rnd.randint(-0x200, 0x200)] for _ in range(2)]
        h = rnd.randint(300, 1200)
        smooth = [rnd.randint(-300, 300), rnd.randint(0, 3000), rnd.randint(-2000, 2000)]
        height_term = rnd.randint(-1500, 1500)
        moved = rnd.randint(0, 1)
        r = cs.SOURCES + 24 * src
        for i, v in enumerate(rec):
            ram.put(r + 4 * i, 'I', v)
        ram.put(r + 20, 'I', h)
        for k in range(2):
            for i in range(3):
                ram.put(cs.TARGETS + 16 * k + 4 * i, 'I', targets[k][i])
        ram.put(cs.PROJECTION, 'H', h)
        sm = cs.SMOOTH + 0x30 * src
        for i, v in enumerate(smooth):
            ram.put(sm + 4 * i, 'I', v)
        ram.put(cs.HEIGHT_TERM, 'I', height_term)
        ram.put(cs.STATE + 0x30 * src + 2, 'H', moved)
        ram.put(cs.GAME_MODE, 'I', 7 if mode7 else 0)
        try:
            cs.camera_frame_fighters(ram, pitch, yaw, reset, distance, src, tuple(ball) if mode7 else None)
        except ZeroDivisionError:
            continue
        after = [ram.s32(r + 4 * i) for i in range(5)] + [ram.s32(sm + 4 * i) for i in range(3)] + \
            [ram.s32(cs.HEIGHT_TERM), ram.u16(cs.STATE + 0x30 * src + 2)]
        out += struct.pack('>6i', src, pitch, yaw, reset, distance, mode7)
        out += struct.pack('>3i5i6ii3iii', *ball, *rec, *targets[0], *targets[1], h, *smooth, height_term, moved)
        out += struct.pack('>5i3iii', *after[:5], *after[5:8], after[8], after[9])
        made += 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{made} cases -> {args.out}')


if __name__ == '__main__':
    main()
