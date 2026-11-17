#!/usr/bin/env python3
"""Export golden cases of Tekken Ball's backdrop placement for tests/test_ball_backdrop.c.

Runs the pinned Tekken3Remake's verified port (`ball_sim.background_draw`, volley.ovl FUN_800B5F0C) on random cameras and view
matrices, one frame per case, and reads back the strip's scroll (the first `_cmod`) and top (the sky tile's top edge).
Per case (big-endian int32): the camera yaw (full), the stored reference yaw, the previous kind, the kind, the round flow's
state, the camera position (3), the view matrix (9 rotation entries, 3 translation, h); then the scroll and the top. The file
begins with the executable's sine table (10,240 bytes), so it is retail-derived and stays under build/. Run it with the
Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_backdrop_cases.py --out build/ball-cases/backdrop.bin
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import ball_sim as bs            # noqa: E402
import draw_sim as ds            # noqa: E402
import projection_sim as pj      # noqa: E402
from fight_sim import Ram       # noqa: E402
from psxcpu import GTE           # noqa: E402

MAGIC = 0x54334244               # 'T3BD'
EXE_BASE = 0x80010000
TRIG_BYTES = 10240
SCENE = 0x801F0000
PACKETS = 0x80100000
MAP = 0x80110000


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=3000)
    parser.add_argument('--seed', type=int, default=5)
    args = parser.parse_args()
    rnd = random.Random(args.seed)
    exe = (ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin').read_bytes()[0x800:]
    trig = exe[pj.SIN_TABLE - EXE_BASE:pj.SIN_TABLE - EXE_BASE + TRIG_BYTES]
    bs.SCRATCH = 0x801FC000
    pj.LOCAL_SCRATCH = 0x801FD000          # the scratchpad words, in RAM here
    mods = []
    real_cmod = bs._cmod

    def cmod(a, b):
        r = real_cmod(a, b)
        mods.append(r)
        return r
    bs._cmod = cmod
    out = bytearray(struct.pack('>3I', MAGIC, 1, args.cases) + trig)
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        ram.data[ram._off(EXE_BASE):ram._off(EXE_BASE) + len(exe)] = exe
        gte = GTE()
        yaw = rnd.choice([rnd.getrandbits(18), rnd.getrandbits(12), rnd.getrandbits(32)])
        ref = rnd.choice([rnd.getrandbits(18), 0, yaw])
        kind = rnd.choice([0, 0, 0, 2])
        last = rnd.choice([kind, kind, kind, 0, 2])
        flow = rnd.choice([0, 1, 1, 2, 3, 5])
        cam = [rnd.randint(-0x20000, 0x20000) for _ in range(3)]
        rot = [rnd.randint(-4096, 4096) for _ in range(9)]
        trans = [rnd.randint(-0x10000, 0x10000) for _ in range(3)]
        h = rnd.randint(200, 1600)
        ram.put(bs.CAMERA + 4, 'I', yaw)
        for i, v in enumerate(cam):
            ram.put(bs.CAMERA + 0x14 + 4 * i, 'I', v)
        for i, v in enumerate(rot):
            ram.put(pj.VIEW + 2 * i, 'H', v)
        for i, v in enumerate(trans):
            ram.put(pj.VIEW + 0x14 + 4 * i, 'I', v)
        gte.write_ctrl(24, 192 << 16)
        gte.write_ctrl(25, 240 << 16)
        gte.write_ctrl(26, h)
        ram.put(bs.YAW_REF, 'I', ref)
        ram.put(bs.MODE_KIND, 'I', kind)
        ram.put(bs.LAST_KIND, 'I', last)
        ram.put(bs.ROUND_FLOW, 'I', flow)
        ram.put(bs.MAP_W, 'I', 32)
        ram.put(bs.MAP_H, 'I', 8)
        ram.put(bs.SCROLL_DIV, 'I', 0x60)
        ram.put(bs.MAP_TEX, 'I', MAP)
        ram.put(bs.MAP_CLUT, 'I', MAP + 0x1000)
        ram.put(bs.BG_PACKETS, 'I', PACKETS)
        ram.put(ds.DISPLAY_BUFFER, 'I', rnd.randint(0, 1))
        ram.put(bs.SCENE_OTS, 'I', SCENE)
        ram.put(SCENE + 4, 'I', SCENE + 0x1000)
        ram.put(bs.SCREEN + 6, 'H', 0x7000)
        mods.clear()
        bs.background_draw(ram, gte)
        buf = ram.u32(ds.DISPLAY_BUFFER)
        top = ram.s16((bs.SKY + 16 * buf) + 0xE)
        out += struct.pack('>5I', yaw, ref, last, kind, flow)
        out += struct.pack('>3i9i3ii', *cam, *rot, *trans, h)
        scroll = mods[0] + 1024 if mods[0] < 0 else mods[0]     # the original adds the width to a negative remainder
        out += struct.pack('>2i', scroll, top)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
