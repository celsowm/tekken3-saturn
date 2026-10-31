#!/usr/bin/env python3
"""Create a Saturn demo ZIP from a locally built mixed-mode disc."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
README = '''TEKKEN 3 V0.1.1 - SATURN DEMO

Open music.cue in a Saturn emulator with a compatible Saturn BIOS, or use a
compatible Saturn disc loader. Keep music.bin alongside music.cue. A BIOS and
emulator are not included. Real hardware has not yet been verified.

Press START at the title, then select ARCADE MODE. Connect a second Saturn
controller before starting for VS mode. All 21 fighters are on the select screen.
Saturn: D-pad movement, X/Y punches, A/B kicks, L guard, R crouch, C jump.
In OPTION MODE, DEBUG enables a performance overlay. START resets a match.

Example Mednafen keyboard bindings: arrows move/step, A/S punch, Z/X kick,
C jump, Q guard, W crouch, Return START. Set these in your emulator.

Unofficial fan project; not affiliated with Bandai Namco. Tekken 3 and its
characters, artwork and sounds belong to their respective owners.
See NOTICE.md and LICENSE for code and research attribution.
'''


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(block)
    return sha.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    build, out = args.build.resolve(), args.out.resolve()
    if (ROOT / 'build').resolve() not in out.parents:
        parser.error('output must be under build/')
    if out.exists():
        parser.error('output already exists')
    cue, binary = build / 'disc/music.cue', build / 'disc/music.bin'
    elf = build / 'tekken3_saturn.elf'
    cue_text = cue.read_text()
    if not cue_text.startswith('FILE "music.bin" BINARY\n') or 'TRACK 02 AUDIO' not in cue_text:
        parser.error('expected mixed-mode CUE pointing to music.bin')
    files = {'music.cue': cue, 'music.bin': binary, 'LICENSE': ROOT / 'LICENSE',
             'NOTICE.md': ROOT / 'NOTICE.md'}
    manifest = {'elf_sha256': digest(elf), 'files': {name: digest(path) for name, path in files.items()}}
    with zipfile.ZipFile(out, 'x', zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        archive.writestr('README.txt', README)
        archive.writestr('MANIFEST.json', json.dumps(manifest, indent=2) + '\n')
        for name, path in files.items():
            archive.write(path, name)
    with zipfile.ZipFile(out) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP verification failed')
    print(json.dumps({'path': str(out), 'sha256': digest(out), 'bytes': out.stat().st_size,
                      'manifest': manifest}, indent=2))


if __name__ == '__main__':
    main()
