#!/usr/bin/env python3
"""Convert a C352 VGM window, build tests/c352_scsp_proto, run it headless on Ymir, capture the SCSP audio and
compare it with the libvgm oracle. Everything it writes stays under --out (private, git-ignored).

    python3 scripts/run_c352_scsp_prototype.py --vgm assets/imported/arcade-vgm/paul.vgz \\
        --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav --end 10 --scale 0.5156 \\
        --out assets/imported/c352-proto/run

Needs the saturn toolchain on PATH, a LibSaturn prefix and the Ymir probe (see saturn_probe.py)."""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import c352_scsp_convert as convert
import compare_c352_scsp as compare

ROOT = Path(__file__).resolve().parents[1]
RESULT_FIELDS = ('magic audio_status driver_status create_status score_status chunks bank_bytes bank_base '
                 'bank_mismatch_words sound_ram_used sound_ram_capacity sound_ram_high_water resident_sounds '
                 'failed_plays voice_steals score_events loop_ticks window_ms tick_before_load tick_after_load '
                 'frames tick_at_frame_a frame_a tick_at_frame_b frame_b heartbeat executed score_executed '
                 'score_loops max_lateness last_lateness queued active_voices_during_play sound_ram_used_during_play '
                 'pg_published pg_played pg_underrun_ticks pg_late_events pg_max_lateness pg_bad_sequence pg_max_burst '
                 'pg_refused pg_executed pg_done pg_base_tick pg_tick_samples pg_abuse_result pg_total_pages '
                 'pg_publish_attempts pg_frame_done cd_mode cd_status cd_bank_read_ticks cd_page_reads '
                 'cd_page_read_ticks_max cd_page_read_ticks_total cd_bank_checksum_file cd_bank_checksum_ram '
                 'dyn_loads dyn_done dyn_late dyn_bytes dyn_load_ticks_max dyn_checksum_errors dyn_initial dyn_initial_ticks '
                 'dyn_margin_min_ticks').split() + [f'pg_margin_ticks_{i}' for i in range(16)]


def sh(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f'{command[0]} failed:\n{result.stdout[-2000:]}{result.stderr[-2000:]}')
    return result.stdout


def symbol(elf, name, nm):
    for line in sh([nm, str(elf)]).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == '_' + name:
            return int(parts[0], 16)
    raise ValueError('missing symbol ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--vgm', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--start', type=float, default=0.0)
    parser.add_argument('--end', type=float, required=True)
    parser.add_argument('--scale', type=float, help='linear gain on every voice (see c352_scsp_convert.py)')
    parser.add_argument('--tick-samples', type=int, default=256, choices=(256, 128, 64))
    parser.add_argument('--legacy-score', action='store_true', help='the 1024-event score instead of pages')
    parser.add_argument('--bank-budget', type=int, help='dynamic bank: load samples section by section into this many bytes')
    parser.add_argument('--loop-passes', type=int, default=1, help='unroll the VGM loop this many times')
    parser.add_argument('--bank-lead', type=float, default=1.5, help='seconds a sample is loaded ahead of its first note')
    parser.add_argument('--guard', action='store_true', help='silence a sample region before its load')
    parser.add_argument('--load-delay-ticks', type=int, default=0, help='test only: hold every fourth load back')
    parser.add_argument('--relative-pages', action='store_true')
    parser.add_argument('--cd', action='store_true', help='read the bank and the pages from a disc image built for the program')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, default=Path(os.environ.get('LIBSATURN_PREFIX', Path.home() / 'dev/saturn-prefix')))
    parser.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    parser.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    parser.add_argument('--iso', type=Path, default=ROOT / 'build/disc/tekken3_saturn.iso', help='any disc image: the probe wants one, the program is injected')
    parser.add_argument('--build', type=Path, default=ROOT / 'build/c352proto')
    parser.add_argument('--nm', default='sh2eb-elf-nm')
    parser.add_argument('--frames', type=int)
    parser.add_argument('--keep-audio', action='store_true', help='keep the raw capture (about 2.6 MB per 15 s)')
    args = parser.parse_args()

    out = args.out.resolve()
    data = out / 'data'
    data.mkdir(parents=True, exist_ok=True)
    pcm8_base = 0x3000
    r = convert.convert(args.vgm, args.start, args.end, pcm8_base, args.scale, tick_samples=args.tick_samples, bank_budget=args.bank_budget, bank_lead_s=args.bank_lead, loop_passes=args.loop_passes)
    manifest = convert.summary(r, argparse.Namespace(start=args.start, end=args.end, pcm8_base=pcm8_base), args.vgm)
    if args.legacy_score and not manifest['scsp']['fits_the_driver_score']:
        sys.exit(f"{manifest['scsp']['events']} events exceed the driver score ({convert.SCORE_MAX_EVENTS}): drop --legacy-score")
    convert.write_outputs(data, r, manifest, args.start, args.end, pcm8_base, args.relative_pages, not args.legacy_score)

    toolchain = args.prefix / 'share/libsaturn/toolchains/sh2eb-elf.cmake'
    sh(['cmake', '-S', str(ROOT / 'tests/c352_scsp_proto'), '-B', str(args.build), '-G', 'Ninja',
        f'-DCMAKE_TOOLCHAIN_FILE={toolchain}', f'-DCMAKE_PREFIX_PATH={args.prefix}', '-DCMAKE_BUILD_TYPE=Release',
        f'-DC352_DATA_DIR={data}', f'-DC352_CD={"ON" if args.cd else "OFF"}', f'-DC352_GUARD={"ON" if args.guard else "OFF"}',
        f'-DC352_LOAD_DELAY_TICKS={args.load_delay_ticks}'])
    sh(['cmake', '--build', str(args.build)])
    elf, binary = args.build / 'c352_scsp_proto.elf', args.build / 'c352_scsp_proto.app.bin'
    address = symbol(elf, 'g_c352_proto', args.nm)
    frames = args.frames or int((args.end - args.start) * 59.94 + 300)
    audio = out / 'audio.s16le'
    iso = next(args.build.glob('**/c352_scsp_proto.iso')) if args.cd else args.iso
    sh([str(args.probe), '--iso', str(iso), '--bin', str(binary), '--bios', str(args.bios),
        '--boot-frames', '90', '--frames', str(frames), '--out', str(out / 'ymir.json'),
        '--dump-mem', f'{address:#x}:{4 * len(RESULT_FIELDS)}:{out / "results.bin"}', '--dump-audio', str(audio)])
    values = struct.unpack(f'>{len(RESULT_FIELDS)}I', (out / 'results.bin').read_bytes())
    values = [v - (1 << 32) if (name.startswith('pg_margin') or name == 'dyn_margin_min_ticks') and v >= 1 << 31 else v for name, v in zip(RESULT_FIELDS, values)]
    results = dict(zip(RESULT_FIELDS, values))
    comparison = compare.compare(args.oracle, audio, args.end - args.start, out / 'compare', args.start)
    report = dict(manifest=manifest, run=results, binary=dict(path=str(binary), bytes=binary.stat().st_size),
                  comparison=comparison)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    if not args.keep_audio:
        audio.unlink()
    print(json.dumps(dict(run=results, scsp=manifest['scsp'], comparison={k: comparison[k] for k in (
        'capture_lag_s', 'level', 'correlation', 'pitch', 'timing')}), indent=2))


if __name__ == '__main__':
    main()
