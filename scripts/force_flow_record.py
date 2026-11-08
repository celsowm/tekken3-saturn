#!/usr/bin/env python3
"""Record a Tekken Force flow trace with the extra RAM the three-fighter comparator seeds from.

Runs the pinned Tekken3Remake `tools/research/flow_trace.py` unchanged, with its range list widened by
the spans `fight_oracle_export.py` reads outside the fighter records (the ground step accumulators, the
body's previous roots, the arena ramp, the HUD, the sound timers, ...), mapped from the USA layout to
Rev.1, plus the whole region around them (the step, root and pair tables hold one entry per fighter
and the third record doubles them). Usage, with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/force_flow_record.py force_run --jobs 1
"""
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


if __name__ == '__main__':
    have = {(a, s) for _, a, s in flow_trace.RANGES}
    flow_trace.RANGES = flow_trace.RANGES + tuple(r for r in extra_ranges() if (r[1], r[2]) not in have)
    sys.argv[0] = 'flow_trace.py'
    raise SystemExit(flow_trace.main())
