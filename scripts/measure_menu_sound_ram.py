#!/usr/bin/env python3
"""Run MENU.BIN on Ymir, press START now and then, and read the menu's audio diagnostics (g_t3_menu_audio):
Sound RAM in use and high water, resident sounds, voice use, steals and failed plays. Writes JSON.

    python3 scripts/measure_menu_sound_ram.py --out build/menu-audio.json [--frames 1500]

Needs the saturn toolchain on PATH (nm), the Ymir probe and a BIOS (see run_c352_scsp_prototype.py)."""
import argparse
import json
import os
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ('magic sfx_ready sound_ram_used sound_ram_capacity sound_ram_high_water resident_sounds resident_capacity '
          'voice_capacity active_voices_max voice_steals failed_plays frames sfx_plays').split()


def symbol(elf, name, nm='sh2eb-elf-nm'):
    for line in subprocess.run([nm, str(elf)], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in (name, '_' + name):
            return int(parts[0], 16)
    raise SystemExit('missing symbol ' + name)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build')
    p.add_argument('--program', default='tekken3_menu')
    p.add_argument('--iso', type=Path)
    p.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--frames', type=int, default=1500)
    p.add_argument('--boot-frames', type=int, default=90)
    p.add_argument('--start-at', type=int, nargs='*', default=[300, 420, 540], help='frames at which START is pressed')
    p.add_argument('--symbol', default='g_t3_menu_audio')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    elf = args.build / (args.program + '.elf')
    iso = args.iso or args.build / 'disc/tekken3_saturn.iso'
    address = symbol(elf, args.symbol)
    with tempfile.TemporaryDirectory() as tmp:
        pad = Path(tmp) / 'pad'
        pad.write_text('\n'.join(['0 NONE'] + [f'{f} START\n{f + 6} NONE' for f in args.start_at]) + '\n')
        dump = Path(tmp) / 'diag.bin'
        cmd = [str(args.probe), '--iso', str(iso), '--bin', str(args.build / (args.program + '.app.bin')),
               '--bios', str(args.bios), '--boot-frames', str(args.boot_frames), '--frames', str(args.frames),
               '--out', str(Path(tmp) / 'run.json'), '--pad-script', str(pad),
               '--dump-mem', f'{address:#x}:{4 * len(FIELDS)}:{dump}']
        subprocess.run(cmd, check=True, capture_output=True)
        values = dict(zip(FIELDS, struct.unpack(f'>{len(FIELDS)}I', dump.read_bytes())))
    values['free_after_sfx'] = values['sound_ram_capacity'] - values['sound_ram_used']
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(values, indent=2) + '\n')
    print(json.dumps(values, indent=2))


if __name__ == '__main__':
    main()
