#!/usr/bin/env python3
"""Record every read the original AiUpdate makes in one frame of a Tekken Force recording, to compare with the
owned bus (T3_AI_TRACE=FRAME T3_AI_TRACE_OUT=FILE in tools/psx_match_replay_eval.c).

Runs the pinned Tekken3Remake flow harness (`flow_trace.py`'s scenario) up to the frame and writes, for each
AiUpdate call of that frame, `A fighter`, then `R address size value` per memory read and `C row 0 result` per
BranchCondition (its own reads left out), the format of fight_oracle_export.py --ai-trace. Addresses are Japan
Rev.1's. Run with the Tekken3Remake venv from its directory:

    work/venv/bin/python ../../scripts/force_ai_trace.py force_run 2475 /tmp/ai-original.txt
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import flow_trace                       # noqa: E402
import fight_harness as fh              # noqa: E402

AI_UPDATE, INPUT_PUSH, BRANCH_CONDITION = 0x80059B78, 0x8002C7D4, 0x8002E310


def main():
    name, target, out = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3])
    from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ
    from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_RA, UC_MIPS_REG_V0
    scenario = next(s for s in flow_trace.SCENARIOS if s.name == name)
    lines, tracing, condition, frame, hooked = [], [False], [None], [0], [False]
    for h, pads in flow_trace.record(scenario):
        if not hooked[0]:
            hooked[0] = True
            cpu = h.cpu

            def read(uc, _access, address, size, _value, _data):
                if tracing[0] and condition[0] is None:
                    lines.append(f'R {address:#x} {size} {int.from_bytes(cpu.read(address, size), "little"):#x}')

            def entry(uc, _address, _size, _data):
                tracing[0] = frame[0] == target
                if tracing[0]:
                    lines.append(f'A {flow_trace.FIGHTERS.index(uc.reg_read(UC_MIPS_REG_A0))}')

            def condition_entry(uc, _address, _size, _data):
                if tracing[0] and condition[0] is None:
                    condition[0] = (uc.reg_read(UC_MIPS_REG_A0), uc.reg_read(UC_MIPS_REG_RA))

            def any_code(uc, address, _size, _data):
                if condition[0] is not None and address == condition[0][1]:
                    lines.append(f'C {condition[0][0]:#x} 0 {int(uc.reg_read(UC_MIPS_REG_V0) != 0):#x}')
                    condition[0] = None

            def push(uc, _address, _size, _data):
                tracing[0] = False
            cpu.uc.hook_add(UC_HOOK_MEM_READ, read)
            cpu.uc.hook_add(UC_HOOK_CODE, any_code)
            cpu.uc.hook_add(UC_HOOK_CODE, condition_entry, begin=BRANCH_CONDITION, end=BRANCH_CONDITION)
            cpu.uc.hook_add(UC_HOOK_CODE, entry, begin=AI_UPDATE, end=AI_UPDATE)
            cpu.uc.hook_add(UC_HOOK_CODE, push, begin=INPUT_PUSH, end=INPUT_PUSH)
        frame[0] += 1
        if frame[0] > target + 1:
            break
    out.write_text(''.join(line + '\n' for line in lines))
    print(f'{out}: {len(lines)} lines')


if __name__ == '__main__':
    main()
