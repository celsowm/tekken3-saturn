#!/usr/bin/env python3
"""Run a Tekken Force disc (a T3_FORCE_INPUT_PROBE build) headless on the Ymir probe and write the rate counters the
probe leaves at tick 600 (g_force_probe_600, as in scripts/mednafen_force_rate.py), a screenshot, and optionally the
VDP1 VRAM/framebuffer and a bounded --debug-profile window analysed per function. Writes OUT/force-rate.json.

    scripts/ymir_force_rate.py --build build/force-work --out build/force-ymir-a --frames 3000
    scripts/ymir_force_rate.py --build build/force-work --out build/force-ymir-p --frames 2000 --profile 1500 48
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import analyze_saturn_profile  # noqa: E402

NM = Path.home() / 'dev/saturn-tools/bin/sh2eb-elf-nm'
WORDS = ('tick', 'input_hash', 'state_hash', 'fields', 'observed_fields', 'discarded_fields', 'scheduled_ticks',
         'images', 'pose_hits', 'pose_misses', 'pose_evictions', 'worst_gap')


def symbol(elf, name):
    out = subprocess.check_output([str(NM), '-S', str(elf)], text=True)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] == name:
            return int(parts[0], 16), int(parts[1], 16)
    raise SystemExit(f'{elf}: no {name} (not a T3_FORCE_INPUT_PROBE build?)')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=3000)
    p.add_argument('--probe', type=Path, default=ROOT.parent / 'lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--dump-vdp1', action='store_true', help='also write vdp1-vram.bin and vdp1-fb.bin')
    p.add_argument('--profile', type=int, nargs=2, metavar=('FROM', 'COUNT'),
                   help='bounded per-CPU --debug-profile window (intrusive: not a rate run)')
    p.add_argument('--trace-capacity', type=int, default=1048576)
    p.add_argument('--timeout', type=int, default=1800)
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True)
    elf = a.build / 'tekken3_force.elf'
    address, size = symbol(elf, '_g_force_probe_600')
    command = [str(a.probe), '--iso', str((a.build / 'disc/tekken3_force.cue').resolve()),
               '--bin', str(a.build / 'tekken3_force.app.bin'), '--bios', str(a.bios.resolve()),
               '--boot-frames', '90', '--frames', str(a.frames), '--sh2-cache', 'on',
               '--out', str(out / 'ymir.json'), '--dump-mem', f'{address:#x}:{size}:{out / "probe-600.bin"}',
               '--screenshot', f'{a.frames - 1}:{out / "screen.png"}',
               '--port-device', '1:pad', '--port-device', '2:pad']
    if a.dump_vdp1:
        command += ['--dump-mem', f'0x05c00000:524288:{out / "vdp1-vram.bin"}', '--dump-fb', str(out / 'vdp1-fb.bin')]
    if a.profile:
        command += ['--debug-profile', str(out / 'debug'), '--trace-from', str(a.profile[0]),
                    '--trace-frames', str(a.profile[1]), '--trace-capacity', str(a.trace_capacity)]
    with (out / 'probe.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=a.timeout)
    words = struct.unpack(f'>{size // 4}I', (out / 'probe-600.bin').read_bytes())
    result = dict(build=str(a.build.resolve()), elf_sha256=sha(elf), frames=a.frames,
                  probe_600=dict(zip(WORDS, words)), reached_tick_600=words[0] == 600,
                  screen_sha256=sha(out / 'screen.png'))
    for name in ('vdp1-vram.bin', 'vdp1-fb.bin'):
        if (out / name).exists():
            result[name.replace('-', '_').replace('.bin', '_sha256')] = sha(out / name)
    if a.profile:
        analysis = analyze_saturn_profile.analyze(out / 'debug', elf, str(NM))
        (out / 'profile.json').write_text(json.dumps(analysis) + '\n')
        result['profile'] = str(out / 'profile.json')
    (out / 'force-rate.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
