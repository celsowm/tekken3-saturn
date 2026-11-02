#!/usr/bin/env python3
"""Validate a user-supplied Tekken 3 Arcade ROM set (read-only) and print a JSON report.

The wave ROM hashes are MAME's for the tekken3 set (C352 sample ROMs). Nothing is copied or modified."""
import argparse
import hashlib
import json
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    print(json.dumps(check(parser.parse_args().directory), indent=2))


if __name__ == '__main__':
    main()
