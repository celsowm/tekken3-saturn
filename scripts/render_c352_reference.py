#!/usr/bin/env python3
"""Render a Namco C352 VGM to a reference WAV with libvgm's C352 core (a port of MAME's c352.cpp).

Builds libvgm (BSD/MIT-style, ValleyBell) from a pinned commit into build/c352ref, runs its vgm2wav at the
chip's native output rate, then writes a manifest with the hashes of the VGM, the ROM set it was checked against,
the libvgm commit, the exact command, and the measured WAV facts. The WAV stays under assets/imported (git-ignored):
it contains the original samples and must not be committed or redistributed."""
import argparse
import array
import hashlib
import json
import math
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_c352_vgm as A  # noqa: E402

LIBVGM_URL = 'https://github.com/ValleyBell/libvgm.git'
LIBVGM_COMMIT = '61b62d64455e039de2a1d01498afa9e9b5f1cb88'


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def build(work):
    exe = work / 'out/bin/vgm2wav'
    if exe.exists():
        return exe
    src = work / 'libvgm'
    if not src.exists():
        work.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', LIBVGM_URL, str(src)], check=True)
    subprocess.run(['git', '-C', str(src), 'checkout', LIBVGM_COMMIT], check=True)
    subprocess.run(['cmake', '-S', str(src), '-B', str(work / 'out'), '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_LIBAUDIO=OFF',
                    '-DBUILD_PLAYER=OFF', '-DBUILD_TESTS=OFF', '-DBUILD_VGM2WAV=ON', '-DUSE_SANITIZERS=OFF',
                    '-DSNDEMU_C352_ALL=ON'], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(work / 'out'), '-j8'], check=True, stdout=subprocess.DEVNULL)
    return exe


def wav_facts(path):
    with wave.open(str(path)) as w:
        rate, frames, channels = w.getframerate(), w.getnframes(), w.getnchannels()
        samples = array.array('h', w.readframes(frames))
    peak = max(max(samples), -min(samples))
    rms = math.sqrt(sum(x * x for x in samples) / len(samples))
    return dict(sample_rate_hz=rate, channels=channels, bits=16, frames=frames, seconds=frames / rate,
                peak=peak, clipped_samples=sum(1 for x in samples if x in (32767, -32768)),
                rms=round(rms, 1), rms_dbfs=round(20 * math.log10(rms / 32768), 2))


def render(vgm, out, rate, loops, exe):
    subprocess.run([str(exe), '--samplerate', str(rate), '--loops', str(loops), '--fade', '0', str(vgm), str(out)],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vgm', type=Path)
    parser.add_argument('--out', type=Path, required=True, help='output WAV (keep it under assets/imported)')
    parser.add_argument('--rate', type=int, default=88200, help='the C352 here outputs 88200 Hz natively')
    parser.add_argument('--loops', type=int, default=2, help='libvgm counts the first play-through as loop 1')
    parser.add_argument('--roms', type=Path, help='ROM directory to hash and compare with the VGM data blocks')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/c352ref')
    args = parser.parse_args()
    exe = build(args.work)
    info = A.parse(A.read_vgm(args.vgm))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    render(args.vgm, args.out, args.rate, args.loops, exe)
    first = sha256(args.out)
    again = args.out.with_suffix('.check.wav')
    render(args.vgm, again, args.rate, args.loops, exe)
    deterministic = sha256(again) == first
    again.unlink()
    header = A.read_vgm(args.vgm)
    total, loop = struct.unpack_from('<II', header, 0x18)[0], struct.unpack_from('<I', header, 0x20)[0]
    manifest = dict(
        method='libvgm C352 core (MAME c352.cpp port), vgm2wav, no resampling at the native rate' if args.rate == 88200
        else 'libvgm C352 core (MAME c352.cpp port), vgm2wav, libvgm resampler',
        libvgm=dict(url=LIBVGM_URL, commit=LIBVGM_COMMIT),
        command=[str(exe), '--samplerate', str(args.rate), '--loops', str(args.loops), '--fade', '0', str(args.vgm), str(args.out)],
        vgm=dict(path=str(args.vgm), sha256=sha256(args.vgm), c352_clock_hz=info['clock'], native_rate_hz=info['out_rate'],
                 seconds=total / 44100, loop_start_s=(total - loop) / 44100, loop_length_s=loop / 44100,
                 rom_data_block_bytes=sum(len(b) for b in info['rom'].values())),
        wav=dict(path=str(args.out), sha256=first, deterministic_second_render=deterministic, **wav_facts(args.out)),
        timeline=dict(intro_s=(total - loop) / 44100, first_pass_end_s=total / 44100,
                      second_pass_ends_s=(total + loop) / 44100 if args.loops >= 2 else None),
        redistribution='proprietary samples inside; keep out of Git')
    if args.roms:
        import check_arcade_rom
        report = check_arcade_rom.check(args.roms)
        manifest['roms'] = {n: f for n, f in report['files'].items() if n.startswith('tet1wave')}
        manifest['vgm_blocks_equal_rom'] = check_arcade_rom.vgm_blocks_match(args.roms, args.vgm)
    manifest_path = args.out.with_suffix('.json')
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
