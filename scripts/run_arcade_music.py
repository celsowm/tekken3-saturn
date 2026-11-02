#!/usr/bin/env python3
"""Convert a whole Arcade song (intro and the loop played N times), build tests/arcade_music_player, run it on Ymir
from a disc image, read g_t3_music back and compare the SCSP capture with the libvgm oracle.

    python3 scripts/run_arcade_music.py --vgm assets/imported/arcade-vgm/paul.vgz \\
        --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav --out assets/imported/c352-proto/full-run

Everything it writes stays under --out (private, git-ignored). Needs the saturn toolchain on PATH, a LibSaturn
prefix and the Ymir probe (see run_c352_scsp_prototype.py)."""
import argparse
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

import c352_scsp_convert as convert
import compare_c352_scsp as compare

ROOT = Path(__file__).resolve().parents[1]
HEAD = 'magic state status step'.split()
META = 'magic version pages loads tick_samples lead_ticks base region_bytes bank_bytes events total_ticks window_ms intro_ms loop_ms loop_passes reserved'.split()
BODY = ('sram_used sram_capacity sram_high_water resident_sounds region_offset base_mismatch sh2_voices_max voice_steals '
        'failed_plays slot_mask page_published page_played page_current events_executed late_events max_lateness_ticks '
        'underrun_ticks bad_sequence refused max_burst stream_tick done loads_total loads_done loads_initial loads_late '
        'load_failures checksum_errors load_ticks_max load_bytes bank_transitions load_margin_min_ticks initial_ticks '
        'page_reads page_read_ticks_max page_read_ticks_total page_read_failures service_calls service_ticks_total '
        'service_ticks_max service_ms_total service_ms_max').split() + [f'service_ms_hist{i}' for i in range(6)]
SEAM = 'stream_tick executed late_events underrun_ticks late_loads bad_sequence page'.split()
SEAMS = 4
SIGNED = {'load_margin_min_ticks'}


def sh(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f'{command[0]} failed:\n{result.stdout[-2000:]}{result.stderr[-2000:]}')
    return result.stdout


def symbol(elf, name, nm):
    for line in sh([nm, str(elf)]).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in (name, '_' + name):
            return int(parts[0], 16)
    raise ValueError('missing symbol ' + name)


def decode(raw):
    words = struct.unpack(f'>{len(raw) // 4}I', raw)
    names = HEAD + ['meta_' + n for n in META] + BODY + [f'seam{k}_{n}' for k in range(SEAMS) for n in SEAM] + ['seams_taken']
    out = {n: (v - (1 << 32) if n in SIGNED and v >= 1 << 31 else v) for n, v in zip(names, words)}
    return out, len(names) * 4


def build(args, data):
    toolchain = args.prefix / 'share/libsaturn/toolchains/sh2eb-elf.cmake'
    sh(['cmake', '-S', str(ROOT / 'tests/arcade_music_player'), '-B', str(args.build), '-G', 'Ninja',
        f'-DCMAKE_TOOLCHAIN_FILE={toolchain}', f'-DCMAKE_PREFIX_PATH={args.prefix}', '-DCMAKE_BUILD_TYPE=Release',
        f'-DMUSIC_DATA_DIR={data}', f'-DMUSIC_SLOTS={args.max_slots}'])
    sh(['cmake', '--build', str(args.build)])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--vgm', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--end', type=float, default=206.07)
    p.add_argument('--loop-passes', type=int, default=2)
    p.add_argument('--max-slots', type=int, default=26)
    p.add_argument('--pcm8-base', type=lambda s: int(s, 0), default=0x3000)
    p.add_argument('--bank-budget', type=int, default=378000)
    p.add_argument('--bank-lead', type=float, default=1.0)
    p.add_argument('--tick-samples', type=int, default=128, choices=(256, 128, 64))
    p.add_argument('--scale', type=float, default=0.5)
    p.add_argument('--frames', type=int)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--prefix', type=Path, default=Path(os.environ.get('LIBSATURN_PREFIX', Path.home() / 'dev/saturn-prefix')))
    p.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--build', type=Path, default=ROOT / 'build/arcade-music')
    p.add_argument('--nm', default='sh2eb-elf-nm')
    p.add_argument('--keep-audio', action='store_true')
    args = p.parse_args()
    out = args.out.resolve()
    data = out / 'data'
    r = convert.convert(args.vgm, 0.0, args.end, args.pcm8_base, args.scale, tick_samples=args.tick_samples,
                        bank_budget=args.bank_budget, bank_lead_s=args.bank_lead, loop_passes=args.loop_passes,
                        max_slots=args.max_slots)
    manifest = convert.summary(r, argparse.Namespace(start=0.0, end=args.end, pcm8_base=args.pcm8_base), args.vgm)
    convert.write_outputs(data, r, manifest, 0.0, args.end, args.pcm8_base, False, True)
    build(args, data)
    iso = next(args.build.glob('**/arcade_music_player.iso'))
    binary = args.build / 'arcade_music_player.app.bin'
    address = symbol(args.build / 'arcade_music_player.elf', 'g_t3_music', args.nm)
    frames = args.frames or int(args.end * 59.94 + 600)
    audio = out / 'audio.s16le'
    size = 4 * (len(HEAD) + len(META) + len(BODY) + SEAMS * len(SEAM) + 1)
    sh([str(args.probe), '--iso', str(iso), '--bin', str(binary), '--bios', str(args.bios), '--boot-frames', '90',
        '--frames', str(frames), '--out', str(out / 'ymir.json'),
        '--dump-mem', f'{address:#x}:{size}:{out / "diag.bin"}', '--dump-audio', str(audio)])
    diag, _ = decode((out / 'diag.bin').read_bytes())
    comparison = compare.compare(args.oracle, audio, args.end, out / 'compare', 0.0)
    report = dict(manifest={k: v for k, v in manifest.items() if k != 'samples'}, diag=diag, comparison=comparison)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    if not args.keep_audio:
        audio.unlink()
    print(json.dumps(dict(diag=diag, comparison={k: comparison[k] for k in ('capture_lag_s', 'level', 'correlation', 'pitch', 'timing')}), indent=2))


if __name__ == '__main__':
    main()
