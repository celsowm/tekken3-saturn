#!/usr/bin/env python3
"""Run the menu's Arcade music test (MENU.BIN built with T3_ARCADE_MUSIC_DIR) on Ymir: boot the menu program on a
disc image that holds ARC*.BIN, open the test from the title with L+R+START, press a menu-sound button now and then,
and read back the engine diagnostics (g_t3_music) and the menu's own (g_t3_menu_audio): Sound RAM measured with the
menu sounds resident, voices, steals, loads, pages, seams. Compares the capture with the libvgm oracle.

    python3 scripts/run_menu_arcade_music.py --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav \\
        --out assets/imported/c352-proto/menu-run

Needs the menu built with the test (see docs/ARCADE_MUSIC_ENGINE.md), the saturn toolchain on PATH and the Ymir
probe. Writes only under --out."""
import argparse
import json
import struct
import subprocess
from pathlib import Path

import compare_c352_scsp as compare
import measure_menu_sound_ram as menu
import run_arcade_music as engine

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--build', type=Path, default=ROOT / 'build')
    p.add_argument('--iso', type=Path)
    p.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--open-at', type=int, default=260, help='frame at which L+R+START opens the test')
    p.add_argument('--sfx-at', type=int, nargs='*', default=[3000, 6000, 9000], help='frames at which A plays a menu sound')
    p.add_argument('--seconds', type=float, default=206.07)
    p.add_argument('--frames', type=int)
    p.add_argument('--no-compare', action='store_true')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    iso = args.iso or args.build / 'disc/tekken3_saturn.iso'
    elf = args.build / 'tekken3_menu.elf'
    music_at = engine.symbol(elf, 'g_t3_music', 'sh2eb-elf-nm')
    menu_at = menu.symbol(elf, 'g_t3_menu_audio')
    pad = ['0 L+R', f'{args.open_at} L+R+START', f'{args.open_at + 6} NONE']
    for f in args.sfx_at:
        pad += [f'{f} A', f'{f + 6} NONE']
    (out / 'pad.txt').write_text('\n'.join(pad) + '\n')
    music_size = 4 * (len(engine.HEAD) + len(engine.META) + len(engine.BODY) + engine.SEAMS * len(engine.SEAM) + 1)
    menu_size = 4 * len(menu.FIELDS)
    frames = args.frames or int(args.open_at + args.seconds * 59.94 + 600)
    audio = out / 'audio.s16le'
    engine.sh([str(args.probe), '--iso', str(iso), '--bin', str(args.build / 'tekken3_menu.app.bin'), '--bios', str(args.bios),
               '--boot-frames', '90', '--frames', str(frames), '--out', str(out / 'ymir.json'), '--pad-script', str(out / 'pad.txt'),
               '--dump-mem', f'{music_at:#x}:{music_size}:{out / "music.bin"}',
               '--dump-mem', f'{menu_at:#x}:{menu_size}:{out / "menu.bin"}', '--dump-audio', str(audio)])
    diag, _ = engine.decode((out / 'music.bin').read_bytes())
    mdiag = dict(zip(menu.FIELDS, struct.unpack(f'>{len(menu.FIELDS)}I', (out / 'menu.bin').read_bytes())))
    report = dict(music=diag, menu=mdiag)
    if not args.no_compare and diag['state'] == 2:
        report['comparison'] = compare.compare(args.oracle, audio, args.seconds, out / 'compare', 0.0)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    audio.unlink(missing_ok=True)
    print(json.dumps(report if args.no_compare else dict(music=diag, menu=mdiag, comparison={
        k: report['comparison'][k] for k in ('capture_lag_s', 'level', 'correlation')} if 'comparison' in report else None), indent=2))


if __name__ == '__main__':
    main()
