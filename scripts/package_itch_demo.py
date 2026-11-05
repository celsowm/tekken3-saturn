#!/usr/bin/env python3
"""Create a Saturn demo ZIP from a locally built mixed-mode disc."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
README = '''TEKKEN 3 V0.3 - SEGA SATURN FAN PORT (DEMO)

HOW TO PLAY
Open music.cue in a Saturn emulator (Mednafen, Ymir...) with a compatible Saturn
BIOS, or use a Saturn disc loader. Keep music.bin next to music.cue. A BIOS and
emulator are not included.

WHAT IS NEW IN V0.3: A REAL ARCADE MODE
- ARCADE MODE is the one-player ten-fight ladder. You pick only your fighter; the
  VS screen shows STAGE 1 to 10 and each opponent is drawn like the original's
  ladder (four of the original ten, four others, Heihachi, then Ogre). The CPU
  gets harder with every fight. All 14 stages are in: every fighter fights on
  its own home stage, with its own music.
- Lose and CONTINUE? counts down from 9 (A/B/C/X/Y/Z cut it short, START
  retries); at 0 it is GAME OVER, then the title.
- At fight 10, win a round against Ogre and he returns as True Ogre.
- After fight 10 the winner's ending movie plays, then a closing screen.
- A NEW CHALLENGER: press START on controller 2 during a fight. Both players
  pick, they fight, and the winner carries the run on.
- VS MODE is the two-player match, as before.
Still from V0.2: the boot intro (LibSaturn logo, then the Tekken 3 opening; START
skips it) and the VS screen shown while the fight loads.

PLAYING
Press START at the title, then ARCADE MODE (one player) or VS MODE (two players,
connect a second controller first).
Saturn pad: D-pad moves, X/Y punches, A/B kicks, L guard, R crouch, C jump.
In OPTION MODE, UP/DOWN picks a row and LEFT/RIGHT changes it: DEBUG shows a
performance report, MUSIC turns the CD music off.
Example Mednafen keys: arrows move, A/S punch, Z/X kick, C jump, Q guard,
W crouch, Return START. Set these in your emulator.

Not in yet: the staff roll, the ending captions, the Ogre cutscene, costumes and
the ranking screen.

REAL SATURN
This build has run in emulators. On a real Saturn it is not verified: the movie
streaming (CD drive speed) and the fight speed and music are untested, and
earlier builds had speed and music problems. If you try it, please send a clip
of the intro and 30 seconds of a fight with DEBUG ON, and say which device you
used (Satiator, SAROO, Fenrir...), its firmware, your Saturn's region, whether
the movies and music played and whether it felt slow.

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
