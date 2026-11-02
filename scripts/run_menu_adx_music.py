#!/usr/bin/env python3
"""Run the menu's ADX stream test (MENU.BIN built with T3_ADX_MUSIC_DIR) on Ymir: boot the menu on a disc image that
holds ADXMUSIC.BIN, open the test from the title with L+R+A and START, press a menu-sound button now and then (the
menu sounds play over the stream), and read back the stream's diagnostics (g_t3_adx_music). The capture is kept for
analyze_ima_stream_lag.py / measure_stereo_skew.py when --keep-audio is given.

    python3 scripts/run_menu_adx_music.py --out assets/imported/adx-proto/menu-run --keep-audio

Needs the disc built with the test (cmake --build build --target tekken3_saturn_disc), the saturn toolchain on PATH
and the Ymir probe. Writes only under --out."""
import argparse
import json
import struct
from pathlib import Path

import measure_menu_sound_ram as menu
import run_arcade_music as engine

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ('magic status step playing underruns rejected_writes min_fill consumed segments waits music_loops').split()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build')
    p.add_argument('--iso', type=Path)
    p.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--open-at', type=int, default=260, help='frame at which L+R+A and START open the test')
    p.add_argument('--sfx-at', type=int, nargs='*', default=[3000, 6000, 9000], help='frames at which B plays a menu sound')
    p.add_argument('--seconds', type=float, default=206.07)
    p.add_argument('--frames', type=int)
    p.add_argument('--keep-audio', action='store_true')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    iso = args.iso or args.build / 'disc/tekken3_saturn.iso'
    elf = args.build / 'tekken3_menu.elf'
    adx_at = engine.symbol(elf, 'g_t3_adx_music', 'sh2eb-elf-nm')
    menu_at = menu.symbol(elf, 'g_t3_menu_audio')
    pad = ['0 L+R+A', f'{args.open_at} L+R+A+START', f'{args.open_at + 6} NONE']
    for f in args.sfx_at:
        pad += [f'{f} B', f'{f + 6} NONE']
    (out / 'pad.txt').write_text('\n'.join(pad) + '\n')
    frames = args.frames or int(args.open_at + args.seconds * 59.94 + 600)
    audio = out / 'audio.s16le'
    engine.sh([str(args.probe), '--iso', str(iso), '--bin', str(args.build / 'tekken3_menu.app.bin'), '--bios', str(args.bios),
               '--boot-frames', '90', '--frames', str(frames), '--out', str(out / 'ymir.json'), '--pad-script', str(out / 'pad.txt'),
               '--dump-mem', f'{adx_at:#x}:{4 * len(FIELDS)}:{out / "adx.bin"}',
               '--dump-mem', f'{menu_at:#x}:{4 * len(menu.FIELDS)}:{out / "menu.bin"}', '--dump-audio', str(audio)])
    adx = dict(zip(FIELDS, struct.unpack(f'>{len(FIELDS)}I', (out / 'adx.bin').read_bytes())))
    mdiag = dict(zip(menu.FIELDS, struct.unpack(f'>{len(menu.FIELDS)}I', (out / 'menu.bin').read_bytes())))
    (out / 'report.json').write_text(json.dumps(dict(adx=adx, menu=mdiag), indent=2))
    if not args.keep_audio:
        audio.unlink(missing_ok=True)
    print(json.dumps(dict(adx=adx, menu=mdiag), indent=2))


if __name__ == '__main__':
    main()
