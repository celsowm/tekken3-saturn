#!/usr/bin/env python3
"""Who writes fighter 0's aim_dir (+0x34) in one frame of a Tekken Force recording, and every call of the original
FighterRelativeAngles (0x80043D2C) in that frame: caller, fighter, its `dist` (+0xF8) at entry.

    work/venv/bin/python ../../scripts/force_angle_trace.py force_run 2824
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import flow_trace                       # noqa: E402

ANGLES = 0x80043D2C


def main():
    name, target = sys.argv[1], int(sys.argv[2])
    from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_RA, UC_MIPS_REG_PC
    scenario = next(s for s in flow_trace.SCENARIOS if s.name == name)
    frame, hooked = [0], [False]
    for h, pads in flow_trace.record(scenario):
        if not hooked[0]:
            hooked[0] = True
            cpu = h.cpu

            def entry(uc, _a, _s, _d):
                if frame[0] != target:
                    return
                f = uc.reg_read(UC_MIPS_REG_A0)
                dist = int.from_bytes(cpu.read(f + 0xF8, 4), 'little')
                print(f'ANGLES fighter {flow_trace.FIGHTERS.index(f) if f in flow_trace.FIGHTERS else hex(f)} '
                      f'opp {uc.reg_read(UC_MIPS_REG_A1):#x} dist {dist} ra {uc.reg_read(UC_MIPS_REG_RA):#x}')

            def write(uc, _access, address, size, value, _d):
                if frame[0] == target and flow_trace.FIGHTERS[0] + 0x34 <= address < flow_trace.FIGHTERS[0] + 0x36:
                    print(f'WRITE aim_dir {value:#x} pc {uc.reg_read(UC_MIPS_REG_PC):#x}')
            cpu.uc.hook_add(UC_HOOK_CODE, entry, begin=ANGLES, end=ANGLES)
            cpu.uc.hook_add(UC_HOOK_MEM_WRITE, write, begin=flow_trace.FIGHTERS[0] + 0x34, end=flow_trace.FIGHTERS[0] + 0x35)
        frame[0] += 1
        if frame[0] > target + 1:
            break


if __name__ == '__main__':
    main()
