#!/usr/bin/env python3
"""Live particle case: run the spray-fixture build and check the particle path.

The build needs T3_PARTICLE_SPRAY_SELFTEST and T3_PARTICLE_CELL_CHECK (see
docs/PARTICLE_INTEGRATION_PLAN.md). The fixture spawns the original's spray at
fixed ticks because no scripted attack here reaches the 21 damage the original
needs. The case asserts that effect images reach the original projection, that
sprites become visible, that real fighter faces are masked, that the cell walk
matches the full scan, and that nothing fails. It does not compare pixels.
"""
import argparse, json, struct, subprocess, sys
from pathlib import Path

SYMBOLS = ['g_t3_ordered_mask_images', 'g_t3_ordered_mask_fallbacks', 'g_t3_mixed_ot_frames',
           'g_t3_mixed_ot_empty_frames', 'g_t3_original_projected',
           'g_t3_original_projected_fallbacks', 'g_t3_particle_images',
           'g_t3_particle_max_sprites', 'g_t3_ordered_mask_tiles', 'g_t3_cell_check_faces',
           'g_t3_cell_check_mismatch', 'g_t3_ordered_mask_cell_drops', 'g_t3_hud_occluded_cells']


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--bios', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--fields', type=int, default=1500)
    a = p.parse_args()
    cmd = [sys.executable, str(Path(__file__).with_name('saturn_probe.py')), '--build', str(a.build),
           '--bios', str(a.bios), '--frames', str(a.fields), '--out', str(a.out)]
    for s in SYMBOLS:
        cmd += ['--dump-symbol', s]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    dumps = json.loads((a.out / 'symbol-dumps.json').read_text())
    v = {}
    for name, entry in dumps.items():
        raw = (a.out / entry['file']).read_bytes()
        v[name.lstrip('_')] = [struct.unpack('>I', raw[i:i + 4])[0] for i in range(0, len(raw) - 3, 4)]
    text = (a.out / 'ymir.json').read_text() if (a.out / 'ymir.json').exists() else ''
    state = {}
    log = a.out.with_name(a.out.name + '.log')
    checks = []

    def check(label, ok):
        checks.append((label, bool(ok)))
    n = v['g_t3_mixed_ot_frames'][0]
    check('effect images reached the original projection', n > 0 and all(0 <= p - n <= 1 for p in v['g_t3_original_projected']))
    check('no projection fallbacks', v['g_t3_original_projected_fallbacks'] == [0, 0])
    check('some images had a visible particle', v['g_t3_mixed_ot_empty_frames'][0] < n)
    check('sprites were captured', v['g_t3_particle_max_sprites'][0] > 0)
    check('real fighter faces were masked', v['g_t3_ordered_mask_tiles'][0] > 0)
    check('cell walk checked against the full scan', v['g_t3_cell_check_faces'][0] > 0)
    check('cell walk matched on every face', v['g_t3_cell_check_mismatch'][0] == 0)
    check('unsupported-face branch exercised (needs T3_PARTICLE_DROP_SELFTEST)',
          v['g_t3_ordered_mask_cell_drops'][0] > 0)
    check('particle cells reached the HUD rectangles and were cleared', v['g_t3_hud_occluded_cells'][0] > 0)
    keep = {k: x for k, x in v.items() if k in SYMBOLS}
    result = {'values': keep, 'checks': dict(checks), 'passed': all(ok for _, ok in checks)}
    (a.out / 'particle-suite.json').write_text(json.dumps(result, indent=2))
    for label, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + label)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
