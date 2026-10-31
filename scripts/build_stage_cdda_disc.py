#!/usr/bin/env python3
"""Build a private mixed-mode Saturn disc with a local stage music WAV."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

from convert_cd_audio import convert


def build(build_dir, wav, libsaturn, force=False):
    build_dir, wav, libsaturn = build_dir.resolve(), wav.resolve(), libsaturn.resolve()
    cache = (build_dir / 'CMakeCache.txt').read_text()
    if 'T3_STAGE_CDDA:BOOL=ON\n' not in cache:
        raise ValueError('build must be configured with T3_STAGE_CDDA=ON')
    iso = build_dir / 'disc/tekken3_saturn.iso'
    output = build_dir / 'disc/music.bin'
    cue = build_dir / 'disc/music.cue'
    if not force and (output.exists() or cue.exists()):
        raise ValueError('music.bin or music.cue exists; use --force to replace them')
    # The local builders are shipped with this project; only the tracked
    # Mode 1 sector encoder is loaded from the LibSaturn source checkout.
    sys.path.insert(1, str(libsaturn / 'tools'))
    from build_cd_audio import build as build_mixed
    with tempfile.TemporaryDirectory(prefix='t3-cdda-', dir=build_dir) as temporary:
        audio = Path(temporary) / 'stage.cdda'
        conversion = convert(wav, audio, 'ffmpeg')
        disc = build_mixed(iso, audio, output, cue)
    return dict(cue=str(cue), binary=str(output), conversion=conversion, disc=disc,
                scope='Private image contains retail-derived assets and non-redistributable boot data.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--wav', type=Path, required=True)
    parser.add_argument('--libsaturn', type=Path, required=True, help='existing LibSaturn source checkout')
    parser.add_argument('--force', action='store_true', help='replace an existing mixed-mode candidate')
    args = parser.parse_args()
    try:
        print(json.dumps(build(args.build, args.wav, args.libsaturn, args.force), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
