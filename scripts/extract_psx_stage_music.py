#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 n0madic
"""Extract one private stage music track from a user-supplied Tekken 3 disc.

Uses the pinned Tekken3Remake XA table and decoder implementation; see its
PolyForm Noncommercial license and NOTICE.md. Outputs contain retail data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/remake_import'))
from common import Disc  # noqa: E402
import media  # noqa: E402


def extract(image, out, track, variant, ffmpeg):
    private_root = (ROOT / 'assets/imported').resolve()
    out = out.resolve()
    if private_root not in out.parents:
        raise ValueError('output must be below the private assets/imported directory')
    if out.exists() and any(out.iterdir()):
        raise ValueError('use an empty output directory')
    if not 0 <= track <= 31 or variant not in (0, 1):
        raise ValueError('track must be 0..31 and variant 0 or 1')
    disc = Disc.open(image)
    try:
        canonical = disc.canonical()
        stream, flags = canonical.exe_u8(media.TRACK_TABLE + 4 * track + 2 * variant, 2)
        if stream & media.CD_DA or stream >= media.XA_ROWS:
            raise ValueError('selected track is not an XA stream')
        start, end, channel = canonical.exe_u32(media.XA_TABLE + 12 * stream, 3)
        sectors, lba = media._xas(canonical)
        raw = b''.join(sectors[(lba + s + channel) * media.SECTOR_BYTES:
                               (lba + s + channel + 1) * media.SECTOR_BYTES]
                       for s in range(start, end + 1, media.STREAM_INTERLEAVE))
        pcm, rate, channels = media._pcm(media.FFmpeg(ffmpeg, frozenset()), raw)
        frames = len(pcm) // (2 * channels)
        if not frames:
            raise ValueError('decoded track is empty')
        wav = media.wav_bytes(pcm, rate, channels, (0, frames) if flags & media.LOOP else None)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'stage.wav').write_bytes(wav)
        pcm_path = out / 'stage.pcm'
        subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y',
                        '-i', str(out / 'stage.wav'), '-ar', '22050', '-ac', '2',
                        '-f', 's8', str(pcm_path)], check=True)
        pcm_size = pcm_path.stat().st_size
        if not pcm_size or pcm_size % 2:
            raise ValueError('invalid Saturn stereo PCM')
        report = dict(track=track, variant=variant, stream=stream, rate_hz=rate,
                      channels=channels, frames=frames, seconds=frames / rate,
                      loop=bool(flags & media.LOOP), volume=flags & media.VOLUME_MASK,
                      wav_sha256=hashlib.sha256(wav).hexdigest(),
                      saturn_pcm_rate_hz=22050, saturn_pcm_bytes=pcm_size,
                      saturn_pcm_sha256=hashlib.sha256(pcm_path.read_bytes()).hexdigest(), private=True)
        (out / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
        return report
    finally:
        disc.image.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--track', type=int, default=7, help='default: Paul stage (7)')
    parser.add_argument('--variant', type=int, choices=(0, 1), default=0,
                        help='0: arrange, 1: arcade')
    parser.add_argument('--ffmpeg', default=shutil.which('ffmpeg'))
    args = parser.parse_args()
    if not args.ffmpeg:
        parser.error('ffmpeg is required')
    try:
        print(json.dumps(extract(args.image, args.out, args.track, args.variant, args.ffmpeg), indent=2))
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
