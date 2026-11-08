#!/usr/bin/env python3
"""The first running Tekken Force frame of a recording, its level and the main loop's random state, for
`force_oracle_export.py --seed-at` and the evaluator's T3_ORIGINAL_MATCH_START.

    third_party/Tekken3Remake/work/venv/bin/python scripts/force_first_running.py <trace>
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
import check_force_flow_trace as cf   # noqa: E402

header, body = cf.load_trace(Path(sys.argv[1]))
for k, (pads, ranges, records) in enumerate(cf.frames(header, body)):
    if len(records) != 3:
        continue
    ram = cf.build_ram(header, ranges, records)
    if ram.s32(0x800AFF50) == 8 and int.from_bytes(records[0][0x3F4:0x3F8], 'little') != 0:
        print(f'first running frame {k}: round state {ram.s32(0x80097350)}, rand {ram.u32(0x800A39B8):#x}, '
              f'level word {ram.s32(0x800B70B0)}')
        break
