#!/usr/bin/env python3
"""Run the W3 strong-hit fixture and report the per-effect-image costs.

The fixture is the combat suite's own input script (close the distance, then
a high hit), run on a boot-to-fight build in headless Ymir. The guest keeps
the worst 32 images that showed hit particles (T3_EFFECT_RING) with the FRT
of every phase of the effect path; this script decodes them and prints the
table, so a candidate and its control can be compared image by image.

The screenshots at the burst are kept for the pixel comparison of W3.4.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'
sys.path.insert(0, str(SCRIPTS))

import saturn_combat_suite  # noqa: E402
import saturn_probe  # noqa: E402

# The same scripts the combat suite uses (scripts/saturn_combat_suite.py):
# the fighters close the distance, then the attack lands.
NEUTRAL = '0 1 buttons=NONE\n0 2 buttons=NONE\n'
SCENARIOS = {
    'high-hit': '20 1 buttons=RIGHT\n100 1 buttons=NONE\n110 2 buttons=LEFT\n120 1 buttons=X\n',
    'simultaneous': ('20 1 buttons=RIGHT\n20 2 buttons=LEFT\n100 1 buttons=NONE\n100 2 buttons=NONE\n'
                     '120 1 buttons=X\n120 2 buttons=X\n'),
    # The high hit does 18 damage and the simultaneous one 8, under the 21 that
    # FUN_80076F10 needs for sparks; the mid and low rows are the combat suite's.
    'mid-hit': '20 1 buttons=RIGHT\n100 1 buttons=NONE\n110 2 buttons=DOWN\n120 1 buttons=RIGHT+DOWN+X\n',
    'low-hit': '20 1 buttons=RIGHT\n100 1 buttons=NONE\n110 2 buttons=NONE\n120 1 buttons=DOWN+A\n',
    # A chain of different attacks from close range; the ring keeps the worst
    # images, so one hit of 21 or more shows up without knowing which move it is.
    'chain': ('20 1 buttons=RIGHT\n100 1 buttons=NONE\n'
              '110 2 buttons=LEFT\n110 1 buttons=X\n140 1 buttons=NONE\n'
              '170 2 buttons=LEFT\n170 1 buttons=Y\n200 1 buttons=NONE\n'
              '230 1 buttons=A\n260 1 buttons=NONE\n'
              '290 1 buttons=B\n320 1 buttons=NONE\n'
              '350 1 buttons=RIGHT+X\n380 1 buttons=NONE\n'
              '410 1 buttons=RIGHT+Y\n440 1 buttons=NONE\n'
              '470 1 buttons=DOWN+X\n500 1 buttons=NONE\n'),
    # Two bursts on the boot-to-fight build: the chain above, then the same
    # chain from P2 (forward is LEFT for P2; P1 holds RIGHT, not guarding).
    # More images reach the ring, so one shifted image weighs less in an A/B.
    'two-chains': ('20 1 buttons=RIGHT\n100 1 buttons=NONE\n'
                   '110 2 buttons=LEFT\n110 1 buttons=X\n140 1 buttons=NONE\n'
                   '170 2 buttons=LEFT\n170 1 buttons=Y\n200 1 buttons=NONE\n'
                   '230 1 buttons=A\n260 1 buttons=NONE\n'
                   '290 1 buttons=B\n320 1 buttons=NONE\n'
                   '350 1 buttons=RIGHT+X\n380 1 buttons=NONE\n'
                   '410 1 buttons=RIGHT+Y\n440 1 buttons=NONE\n'
                   '470 1 buttons=DOWN+X\n500 1 buttons=NONE\n'
                   '560 2 buttons=NONE\n560 1 buttons=RIGHT\n'
                   '570 2 buttons=X\n600 2 buttons=NONE\n'
                   '630 2 buttons=Y\n660 2 buttons=NONE\n'
                   '690 2 buttons=A\n720 2 buttons=NONE\n'
                   '750 2 buttons=B\n780 2 buttons=NONE\n'
                   '810 2 buttons=LEFT+X\n840 2 buttons=NONE\n'
                   '870 2 buttons=LEFT+Y\n900 2 buttons=NONE\n'
                   '930 2 buttons=DOWN+X\n960 2 buttons=NONE\n'),
    # A throw: throws do more damage than the strikes above and are not blocked.
    'throw': '20 1 buttons=RIGHT\n100 1 buttons=NONE\n110 2 buttons=NONE\n120 1 buttons=X+Y\n',
    'blocked': '20 1 buttons=RIGHT\n100 1 buttons=NONE\n110 2 buttons=DOWN\n120 1 buttons=X\n',
    'miss': '20 1 buttons=LEFT\n100 1 buttons=NONE\n120 1 buttons=X\n',
    # The attract demonstration is not reachable through Ymir's injection mode
    # (the frontend hands over to MENU.BIN, whose symbols stop updating), so
    # the fixture uses the same arcade path as scripts/ai_cost.py: the title,
    # the mode list and both picks, then the CPU fighter hits the idle one.
    'arcade': ('400 1 buttons=START\n406 1 buttons=NONE\n700 1 buttons=START\n706 1 buttons=NONE\n'
               '2100 1 buttons=START\n2106 1 buttons=NONE\n2160 1 buttons=START\n2166 1 buttons=NONE\n'
               '2400 1 buttons=START\n2406 1 buttons=NONE\n'),
}
SYMBOLS = ('_g_t3_effect_ring', '_g_t3_effect_ring_count', '_g_t3_effect_ring_images',
           '_g_t3_mixed_ot_frames', '_g_t3_mixed_ot_particles', '_g_t3_mixed_ot_peak',
           '_g_t3_ordered_mask_drawn_images', '_g_t3_ordered_mask_drawn_particles',
           '_g_t3_ordered_mask_dropped_particles', '_g_t3_original_projected',
           '_g_t3_original_ot_frames', '_g_t3_original_ot_faces', '_g_t3_original_ot_culled')
# The in-build equivalence check (T3_MIXED_OT_LIVE_ENTRIES_CHECK) is only in
# the build that carries it; the probe refuses a missing dump symbol.
CHECK_SYMBOLS = ('_g_t3_live_entries_check_images', '_g_t3_live_entries_check_mismatch',
                 '_g_t3_live_entries_check_sum', '_g_t3_live_entries_check_entries',
                 '_g_t3_live_entries_check_faces', '_g_t3_live_entries_check_cells',
                 '_g_t3_outcodes_check_faces', '_g_t3_outcodes_check_rejected',
                 '_g_t3_outcodes_check_mismatch')


def elf_symbols(build):
    nm = shutil.which('sh2eb-elf-nm') or str(ROOT.parent / 'saturn-tools/bin/sh2eb-elf-nm')
    return saturn_probe.symbols(build / 'tekken3_saturn.elf', nm)


def build_script(path, scenario, offset):
    lines = [NEUTRAL.rstrip('\n')]
    for line in SCENARIOS[scenario].strip().splitlines():
        field, rest = line.split(None, 1)
        lines.append(f'{int(field) + offset} {rest}')
    path.write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--scenario', choices=sorted(SCENARIOS), default='high-hit')
    parser.add_argument('--frames', type=int, default=400,
                        help='fields to run after the scripted hit')
    parser.add_argument('--offset', type=int, default=None,
                        help='fields to wait before the scripted hit (measured when absent)')
    parser.add_argument('--bios', type=Path,
                        default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    parser.add_argument('--libsaturn', type=Path, default=ROOT.parent / 'lib-saturn')
    parser.add_argument('--shot', type=int, nargs='+', default=[],
                        help='emulator frames to keep as screenshots')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    probe_args = saturn_probe.parser().parse_args([
        '--build', str(args.build), '--disc', str(args.build / 'disc/music.cue'),
        '--bios', str(args.bios), '--libsaturn', str(args.libsaturn),
        '--out', str(args.out / 'probe'), '--frames', '200', '--port2', 'pad',
        '--workload', 'diagnostic'])
    offset = args.offset
    if offset is None and args.scenario not in ('attract', 'arcade'):
        # The combat suite measures the first controllable fight field the
        # same way; the boot-to-fight build reaches it after the CD loads.
        probe_args.out = args.out / 'offset'
        probe_args.out.mkdir(parents=True, exist_ok=True)
        offset = saturn_combat_suite.first_gameplay_field(probe_args)
        print(f'first gameplay field {offset}')
    elif offset is None:
        offset = 0
    script = args.out / 'input.devices'
    build_script(script, args.scenario, offset)
    command = [sys.executable, str(SCRIPTS / 'saturn_probe.py'),
               '--build', str(args.build), '--disc', str(args.build / 'disc/music.cue'),
               '--bios', str(args.bios), '--libsaturn', str(args.libsaturn),
               '--out', str(args.out / 'probe'), '--frames', str(offset + args.frames),
               '--timeout', '900', '--device-script', str(script),
               '--sh2-cache', 'on', '--port2', 'pad']
    for symbol in SYMBOLS:
        command += ['--dump-symbol', symbol]
    syms = elf_symbols(args.build)
    for symbol in CHECK_SYMBOLS:
        if symbol in syms:
            command += ['--dump-symbol', symbol]
    for frame in args.shot:
        command += ['--screenshot-frame', str(frame)]
    subprocess.run(command, check=True)
    observation = json.loads((args.out / 'probe' / 'observation.json').read_text())
    state = observation.get('state', observation)
    manifest = json.loads((args.out / 'probe' / 'symbol-dumps.json').read_text())
    ring = manifest.get('_g_t3_effect_ring')
    if not ring:
        raise SystemExit('the build has no effect ring (configure with -DT3_EFFECT_RING=ON)')
    print(f"scenario {args.scenario}: hits {state.get('psx_registered_hits')} "
          f"blocks {state.get('psx_registered_blocks')} "
          f"health {state.get('psx_health_q16')} "
          f"sim_ticks {state.get('sim_ticks')} frames {state.get('frame_serial')} "
          f"first_error {state.get('first_error')}")
    report = [sys.executable, str(SCRIPTS / 'report_effect_ring.py'),
              '--bin', str(args.out / 'probe' / ring['file'])]
    subprocess.run(report, check=True)
    report_check(args.out / 'probe', manifest)


def report_check(probe, manifest):
    """Print the in-build equivalence checks, when the build carries them."""
    def words(name, count=1):
        raw = (probe / manifest[name]['file']).read_bytes()
        return [int.from_bytes(raw[i:i + 4], 'big') for i in range(0, len(raw) - 3, 4)]
    if '_g_t3_outcodes_check_faces' in manifest:
        tested = words('_g_t3_outcodes_check_faces')[0]
        rejected = words('_g_t3_outcodes_check_rejected')[0]
        mismatch = words('_g_t3_outcodes_check_mismatch')[0]
        print(f'outcodes check: {tested} faces tested, {rejected} rejected by outcodes, '
              f'{mismatch} of those reach a covered cell')
        if mismatch:
            raise SystemExit('the outcodes rejected a face the coverage test keeps')
    if '_g_t3_live_entries_check_images' not in manifest:
        return
    images = words('_g_t3_live_entries_check_images')[0]
    mismatch = words('_g_t3_live_entries_check_mismatch')[0]
    sums = words('_g_t3_live_entries_check_sum', 2)
    entries = words('_g_t3_live_entries_check_entries', 2)
    faces = words('_g_t3_live_entries_check_faces', 2)
    cells = words('_g_t3_live_entries_check_cells', 2)
    print(f'live-entries check: {images} images, {mismatch} mismatches')
    print(f'  full list:     entries {entries[0]} faces {faces[0]} cells {cells[0]} sum {sums[0]:08x}')
    print(f'  compacted list: entries {entries[1]} faces {faces[1]} cells {cells[1]} sum {sums[1]:08x}')
    if mismatch:
        raise SystemExit('the compacted entry list published different cells')


if __name__ == '__main__':
    main()
