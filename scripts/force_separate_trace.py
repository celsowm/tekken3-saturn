#!/usr/bin/env python3
"""Print the three fighters' anchor, root and facing every time the original's BodySeparate (0x8004401C) is called in one
frame of a Tekken Force recording, with the pair it is called for.

    work/venv/bin/python ../../scripts/force_separate_trace.py force_final 5660
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
import flow_trace                       # noqa: E402

SEPARATE = 0x8004401C


def main():
    name, target = sys.argv[1], int(sys.argv[2])
    from unicorn import UC_HOOK_CODE
    from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1
    scenario = next(s for s in flow_trace.SCENARIOS if s.name == name)
    frame, hooked = [0], [False]
    for h, pads in flow_trace.record(scenario):
        if not hooked[0]:
            hooked[0] = True
            cpu = h.cpu

            def s32(a):
                return int.from_bytes(cpu.read(a, 4), 'little', signed=True)

            def u16(a):
                return int.from_bytes(cpu.read(a, 2), 'little')

            def entry(uc, _a, _s, _d):
                if frame[0] != target:
                    return
                a, b = uc.reg_read(UC_MIPS_REG_A0), uc.reg_read(UC_MIPS_REG_A1)
                for label, f in (('a', a), ('b', b)):
                    print(f'{label} fighter {flow_trace.FIGHTERS.index(f)}: anchor {s32(f)},{s32(f + 8)} root {s32(f + 0xF68)},{s32(f + 0xF70)} '
                          f'facing {u16(f + 0xE):#x}')
            cpu.uc.hook_add(UC_HOOK_CODE, entry, begin=SEPARATE, end=SEPARATE)
        frame[0] += 1
        if frame[0] > target + 1:
            break


if __name__ == '__main__':
    main()
