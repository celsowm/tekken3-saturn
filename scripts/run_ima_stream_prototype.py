#!/usr/bin/env python3
"""Play the IMA-encoded Paul theme from the CD on Ymir (tests/ima_stream_proto), capture the SCSP output and compare
it with the libvgm oracle. A second comparison uses the exact decode of the same blocks (no Saturn in the path): the
gap between the two is what the Saturn stream adds, and the software decode alone is what the codec costs.

    python3 scripts/encode_ima_stream.py --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav \\
        --out assets/imported/ima-proto/paul
    python3 scripts/run_ima_stream_prototype.py --data assets/imported/ima-proto/paul \\
        --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav --out assets/imported/ima-proto/run

Everything it writes stays under --out (private, git-ignored). The audio capture and the aligned WAVs are removed
unless --keep-audio is given; compare.json and report.json are kept."""
import argparse
import json
import os
import struct
from pathlib import Path

import compare_c352_scsp as compare
from run_c352_scsp_prototype import sh, symbol

ROOT = Path(__file__).resolve().parents[1]
RESULT_FIELDS = (
    'magic audio_status cd_status stream_status source_status asset_status open_status play_status '
    'track_bytes track_frames track_rate track_channels frames playing_frame first_loop_frame '
    'sound_ram_used sound_ram_high_water active_voices '
    'stream_underruns stream_refills stream_consumed stream_min_fill stream_rejected '
    'disc_segments disc_bytes disc_music_segments disc_music_loops disc_data_deferred disc_waits disc_min_buffered '
    'disc_max_ticks_poll disc_max_ticks_music disc_max_ticks_data disc_standby_visits disc_standby_worst_dip '
    'ticks_per_second sizeof_disc_stream first_error').split()
MAGIC = 0x494d4131                     # 'IMA1'
RATE = compare.RATE


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--data', type=Path, required=True, help='encoder output: ima.bin, ima_meta.h, ima_decoded_44100.s16le')
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, default=Path(os.environ.get('LIBSATURN_PREFIX', Path.home() / 'dev/saturn-prefix')))
    parser.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    parser.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    parser.add_argument('--build', type=Path, default=ROOT / 'build/imaproto')
    parser.add_argument('--nm', default='sh2eb-elf-nm')
    parser.add_argument('--frames', type=int, help='display frames to run (default: the track plus a margin)')
    parser.add_argument('--keep-audio', action='store_true')
    parser.add_argument('--control-only', action='store_true', help='skip Ymir: compare only the software decode')
    args = parser.parse_args()

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    encode = json.loads((args.data / 'encode.json').read_text())
    seconds = encode['seconds']
    comparisons = {}

    control = out / 'control'
    comparisons['software_decode'] = compare.compare(args.oracle, args.data / 'ima_decoded_44100.s16le', seconds,
                                                     control, 0.0)
    if args.control_only:
        report = dict(encode=encode, comparisons=comparisons)
        (out / 'report.json').write_text(json.dumps(report, indent=2))
        print(json.dumps({k: {m: v[m] for m in ('capture_lag_s', 'level', 'correlation', 'pitch', 'timing')}
                          for k, v in comparisons.items()}, indent=2))
        return

    toolchain = args.prefix / 'share/libsaturn/toolchains/sh2eb-elf.cmake'
    sh(['cmake', '-S', str(ROOT / 'tests/ima_stream_proto'), '-B', str(args.build), '-G', 'Ninja',
        f'-DCMAKE_TOOLCHAIN_FILE={toolchain}', f'-DCMAKE_PREFIX_PATH={args.prefix}', '-DCMAKE_BUILD_TYPE=Release',
        f'-DIMA_DATA_DIR={args.data.resolve()}'])
    sh(['cmake', '--build', str(args.build)])
    elf, binary = args.build / 'ima_stream_proto.elf', args.build / 'ima_stream_proto.app.bin'
    address = symbol(elf, 'g_ima_proto', args.nm)
    frames = args.frames or int(seconds * 59.94 + 300)
    audio = out / 'audio.s16le'
    iso = next(args.build.glob('**/ima_stream_proto.iso'))
    sh([str(args.probe), '--iso', str(iso), '--bin', str(binary), '--bios', str(args.bios),
        '--boot-frames', '90', '--frames', str(frames), '--out', str(out / 'ymir.json'),
        '--dump-mem', f'{address:#x}:{4 * len(RESULT_FIELDS)}:{out / "results.bin"}', '--dump-audio', str(audio)])
    values = struct.unpack(f'>{len(RESULT_FIELDS)}I', (out / 'results.bin').read_bytes())
    results = dict(zip(RESULT_FIELDS, values))
    if results['magic'] != MAGIC:
        raise SystemExit(f"result block magic {results['magic']:#x}: the program did not run")

    comparisons['saturn_stream'] = compare.compare(args.oracle, audio, seconds, out / 'compare', 0.0)
    report = dict(encode=encode, run=results, binary=dict(path=str(binary), bytes=binary.stat().st_size),
                  comparisons=comparisons)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    if not args.keep_audio:
        audio.unlink()
        for wav in (out / 'compare').glob('*.wav'):
            wav.unlink()
        for wav in control.glob('*.wav'):
            wav.unlink()
    print(json.dumps(dict(run=results, comparisons={k: {m: v[m] for m in (
        'capture_lag_s', 'level', 'correlation', 'pitch', 'timing')} for k, v in comparisons.items()}), indent=2))


if __name__ == '__main__':
    main()
