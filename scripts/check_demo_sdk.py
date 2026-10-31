#!/usr/bin/env python3
"""Check the installed LibSaturn archive for this demo's known SDK requirements.

The sound-size check applies to the registry ABI at LibSaturn d9787ca. An
unknown layout is reported as unverified.
"""
import argparse
import os
from pathlib import Path
import subprocess

REGISTRY_SUFFIX = '16g_sound_registryE'
REGISTRY_ENTRY_BYTES = 28
# The fixed effects and system sounds, plus two fighters of up to 18 voices.
MIN_SOUNDS = 61


def inspect_symbols(output):
    symbols = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 4:
            continue
        try:
            size = int(fields[1], 16)
        except ValueError:
            continue
        symbols[fields[3]] = (fields[2], size)
    if not any(name.lstrip('_') == 'sat_vdp2_rotation_bitmap_configure'
               and kind.upper() == 'T' for name, (kind, _) in symbols.items()):
        raise ValueError('installed LibSaturn lacks the rotation-bitmap API; update it to d9787ca or later and reinstall')
    if not any(name.lstrip('_') == 'sat_character_asset_compact'
               and kind.upper() == 'T' for name, (kind, _) in symbols.items()):
        raise ValueError('installed LibSaturn lacks the compact character asset; update it to d9787ca or later and reinstall')
    matches = [(name, kind, size) for name, (kind, size) in symbols.items()
               if name.endswith(REGISTRY_SUFFIX)]
    if len(matches) != 1:
        raise ValueError('cannot verify the installed sound registry; unknown or stripped LibSaturn archive')
    _, kind, size = matches[0]
    if kind.upper() != 'B' or size % REGISTRY_ENTRY_BYTES:
        raise ValueError('cannot verify the installed sound registry layout')
    capacity = size // REGISTRY_ENTRY_BYTES
    if capacity < MIN_SOUNDS:
        raise ValueError(f'installed LibSaturn has {capacity} sound slots; the demo needs at least {MIN_SOUNDS}; update it to d9787ca or later and reinstall')
    return capacity


def check(prefix, nm=None):
    archive = Path(prefix) / 'lib/libsaturn.a'
    if not archive.is_file():
        raise ValueError(f'missing installed LibSaturn archive: {archive}')
    if nm is None:
        triplet = os.environ.get('LIBSATURN_TOOLCHAIN_PREFIX', 'sh2eb-elf')
        root = os.environ.get('LIBSATURN_TOOLCHAIN_ROOT')
        nm = str(Path(root) / (triplet + '-nm')) if root else triplet + '-nm'
    output = subprocess.check_output([nm, '-S', '--defined-only', str(archive)], text=True)
    return inspect_symbols(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--nm', help='SH-2 nm executable; defaults to the LibSaturn toolchain')
    args = parser.parse_args()
    try:
        capacity = check(args.prefix, args.nm)
        print(f'Installed SDK exposes the bitmap-floor API and a {capacity}-slot sound registry (known ABI).')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
