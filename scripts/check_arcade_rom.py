#!/usr/bin/env python3
"""Validate a user-supplied Tekken 3 Arcade ROM set (read-only) and print a JSON report.

The wave ROM hashes are MAME's for the tekken3 set (C352 sample ROMs). Nothing is copied or modified."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

WAVE = {'tet1wave0.5': 'fe9434dcf0fb232c85efaaae1b4b13d36099620a',
        'tet1wave1.4': '941412bbe9d0305d9a23c224c1bb774c4321f6df'}
SOUND_PROGRAM = 'tet1verb.11s'
MAIN_PROGRAM = ('tet1vere.2e', 'tet1vere.2j')


def sha1(path):
    return hashlib.sha1(path.read_bytes()).hexdigest()


def check(directory):
    report = {'directory': str(directory), 'files': {}, 'wave_ok': True}
    for path in sorted(directory.iterdir()):
        if path.is_file():
            report['files'][path.name] = dict(bytes=path.stat().st_size, sha1=sha1(path))
    for name, expected in WAVE.items():
        found = report['files'].get(name)
        ok = bool(found) and found['sha1'] == expected
        report['wave_ok'] &= ok
        report['files'].setdefault(name, {})['mame_match'] = ok
    report['sound_program_present'] = SOUND_PROGRAM in report['files']
    report['main_program_present'] = all(n in report['files'] for n in MAIN_PROGRAM)
    return report


def vgm_blocks_match(directory, vgm):
    """How many of a C352 VGM's ROM data blocks equal wave0 + wave1 at their own addresses."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import analyze_c352_vgm
    rom = (directory / 'tet1wave0.5').read_bytes() + (directory / 'tet1wave1.4').read_bytes()
    blocks = analyze_c352_vgm.parse(analyze_c352_vgm.read_vgm(vgm))['rom']
    return dict(blocks=len(blocks), matching=sum(1 for a, b in blocks.items() if rom[a:a + len(b)] == b))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--vgm', type=Path, help='also check that this VGM carries the ROM bytes of this set')
    args = parser.parse_args()
    report = check(args.directory)
    if args.vgm:
        report['vgm_blocks'] = vgm_blocks_match(args.directory, args.vgm)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
