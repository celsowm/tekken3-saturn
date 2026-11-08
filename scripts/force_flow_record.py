#!/usr/bin/env python3
"""Record a Tekken Force flow trace with the extra RAM the three-fighter comparator seeds from.

Runs the pinned Tekken3Remake `tools/research/flow_trace.py` unchanged, with its range list widened by
the spans `fight_oracle_export.py` reads outside the fighter records (the ground step accumulators, the
body's previous roots, the arena ramp, the HUD, the sound timers, ...), mapped from the USA layout to
Rev.1, plus the whole region around them (the step, root and pair tables hold one entry per fighter
and the third record doubles them). Usage, with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/force_flow_record.py force_run --jobs 1
"""
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(RESEARCH))

import fight_oracle_export as ox   # noqa: E402
import flow_trace                  # noqa: E402

# Rev.1 regions around the seeds (STEP_X/STEP_Z 0x8009E9A8/B8, PREVIOUS 0x8009EAA0, RAMP 0x80097E68):
# the pair table already recorded is 0x8009EA08+0x60.
WIDE = (('step_region', 0x8009E990, 0x80), ('previous_region', 0x8009EA68, 0xC0), ('ramp_region', 0x80097E50, 0x40))


def extra_ranges():
    layout = ox.Layout(ox.LAYOUT)
    out = list(WIDE)
    for k, (usa, size) in enumerate(ox.GLOBALS):
        out.append((f'oracle{k}', layout.to_jp(usa, size), size))
    return tuple(out)


INPUT_PUSH = 0x8002C7D4   # FighterInputPush(fighter, ?, held, pressed): the pads of every fighter, the AI's included


def record_with_pads(scenario):
    """flow_trace.record plus a side file `flow_<scenario>.pads` of every input push: u32 frame, u8 fighter,
    u8 pad, u16 held, u16 pressed (the trace's frame index; the first frame is not hooked)."""
    from unicorn import UC_HOOK_CODE
    from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A2, UC_MIPS_REG_A3
    limit = int(os.environ.get('T3_FLOW_MAX_FRAMES', '0'))
    pushes, frame, hooked = [], [0], [False]
    try:
        for h, pads in _record(scenario):
            if not hooked[0]:
                hooked[0] = True

                def push(uc, _address, _size, _data):
                    f = uc.reg_read(UC_MIPS_REG_A0)
                    if f in flow_trace.FIGHTERS:
                        pushes.append((frame[0], flow_trace.FIGHTERS.index(f), uc.reg_read(UC_MIPS_REG_A2) & 0xFFFF,
                                       uc.reg_read(UC_MIPS_REG_A3) & 0xFFFF))
                h.cpu.uc.hook_add(UC_HOOK_CODE, push, begin=INPUT_PUSH, end=INPUT_PUSH)
            frame[0] += 1
            yield h, pads
            if limit and frame[0] >= limit:
                break
    finally:
        out = flow_trace.OUT_DIR / f'flow_{scenario.name}.pads'
        out.write_bytes(b''.join(struct.pack('<IBxHH', *p[:2], *p[2:]) for p in pushes))


_record = flow_trace.record
flow_trace.record = record_with_pads


if __name__ == '__main__':
    have = {(a, s) for _, a, s in flow_trace.RANGES}
    flow_trace.RANGES = flow_trace.RANGES + tuple(r for r in extra_ranges() if (r[1], r[2]) not in have)
    sys.argv[0] = 'flow_trace.py'
    raise SystemExit(flow_trace.main())
