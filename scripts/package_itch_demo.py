#!/usr/bin/env python3
"""Create a Saturn demo ZIP from a locally built mixed-mode disc."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from play_mednafen import missing_features

ROOT = Path(__file__).resolve().parents[1]
README = '''TEKKEN 3 V0.4 - SEGA SATURN FAN PORT (DEMO)

HOW TO PLAY
Open music.cue in a Saturn emulator (Mednafen, Ymir...) with a compatible Saturn
BIOS, or use a Saturn disc loader. Keep music.bin next to music.cue. A BIOS and
emulator are not included.

WHAT IS NEW IN V0.4: TEKKEN FORCE
- TEKKEN FORCE MODE is in. Pick your fighter and walk through the four
  side-scrolling levels against Crow, Falcon, Hawk and Owl, up to two at a time,
  with each level's boss at its end. The level script, enemy AI, camera and walls
  are ported from the original's code and checked frame by frame against
  recordings of the original.
- The timer, the bonus seconds per defeat, roast chicken, LIFE UP!, the keys,
  NOW LOADING, STAGE CLEAR!!, the tally and the result screen, in the original's
  art, fonts and sounds.
- Three keys open the final stage against Doctor B. The keys, the high score and
  Doctor B. are saved to the Saturn's internal memory (a few free blocks; it
  never formats the memory).
- Smoother fights: the benchmark fight runs at about 29.5 images per second in
  Mednafen, against about 25.6 before this version.
Still from V0.3: ARCADE MODE (the ten-fight ladder with all 14 stages, CONTINUE?,
True Ogre, the endings and NEW CHALLENGER), VS MODE, the boot intro and the VS
screen.

PLAYING
Press START at the title, then ARCADE MODE (one player), VS MODE (two players,
connect a second controller first) or TEKKEN FORCE MODE (one player).
Saturn pad: D-pad moves, X/Y punches, A/B kicks, L guard, R crouch, C jump.
In OPTION MODE, UP/DOWN picks a row and LEFT/RIGHT changes it: DEBUG shows a
performance report, MUSIC turns the CD music off.
Example Mednafen keys: arrows move, A/S punch, Z/X kick, C jump, Q guard,
W crouch, Return START. Set these in your emulator.

Not in yet: Team Battle, the staff roll, the ending captions, the Ogre cutscene,
costumes and the ranking screen.

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
    missing = missing_features(cue)
    if missing is None or missing:
        parser.error('not the full game (scripts/play_mednafen.py would refuse it): '
                     + ('; '.join(missing) if missing else 'no staged disc root'))
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
