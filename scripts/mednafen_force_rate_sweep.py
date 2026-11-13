#!/usr/bin/env python3
"""Rate A/B over several layouts or starts. The probe's tick-600 counts move by an image, and its worst gap by a
field, with code layout alone (Mednafen models the SH-2 cache) and with the level's start. This rebuilds a
T3_FORCE_INPUT_PROBE build once per value of the varied cache setting (--vary: T3_FORCE_PROBE_TEXT_PAD, bytes of code
ahead of the game library, the default; it recompiles one object, or T3_FORCE_PROBE_START_FIELDS, which rebuilds the
target), runs scripts/mednafen_force_rate.py on each, deletes the run directories and prints one row per value plus
the sum of images and the worst gap. Run it on the candidate and the control settings and compare the totals.

    scripts/mednafen_force_rate_sweep.py --build build/force-work --out build/frate-sweep --values 0 1040 2064 3088 \
        --define T3_FORCE_SFX=ON
"""
import argparse
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--out', type=Path, required=True, help='run directory prefix; each run is removed after reading')
p.add_argument('--vary', default='T3_FORCE_PROBE_TEXT_PAD')
p.add_argument('--values', type=int, nargs='+', default=[0, 1040, 2064, 3088])
p.add_argument('--define', action='append', default=[], help='extra CMake cache setting, NAME=VALUE')
p.add_argument('--seconds', type=float, default=70)
a = p.parse_args()

defines = ['-D' + d for d in a.define]
images = 0
worst = 0
for start in a.values:
    subprocess.run(['cmake', *defines, f'-D{a.vary}={start}', str(a.build)], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(a.build)], check=True, stdout=subprocess.DEVNULL)
    run = Path(f'{a.out}-{start}')
    if run.exists():
        shutil.rmtree(run)
    try:
        text = subprocess.check_output(['python3', str(ROOT / 'scripts/mednafen_force_rate.py'), '--build',
                                        str(a.build), '--out', str(run), '--seconds', str(a.seconds)], text=True)
    finally:
        if run.exists():
            shutil.rmtree(run)
    words = re.search(r'_g_force_probe_600 \S+ \d+: \[([^\]]*)\]', text)
    if not words:
        raise SystemExit(f'{a.vary}={start}: no probe counters\n{text}')
    w = [int(x) for x in words.group(1).split(',')]
    if w[0] != 600:
        raise SystemExit(f'{a.vary}={start}: the probe did not reach tick 600: {w}')
    images += w[7]
    worst = max(worst, w[11])
    print(f'{a.vary}={start}: images {w[7]} worst gap {w[11]} scheduled {w[6]} state {w[2]}', flush=True)
subprocess.run(['cmake', f'-D{a.vary}=0', str(a.build)], check=True, stdout=subprocess.DEVNULL)
print(f'total: images {images} over {len(a.values)} values, worst gap {worst}')
