#!/usr/bin/env python3
"""Open a built disc in Mednafen to play it, with sound and the whole picture.

The title and select run 704 wide. With ss.correct_aspect on, Mednafen keeps
the dots where they are and cuts the picture at 660, so the last 44 dots on the
right are lost and centred art looks shifted right. Both settings are passed
for this run only.
"""
import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# What the playable game must carry. A build configured without the private inputs still boots, but
# silently drops the feature: no opening movie, the plain loading bar instead of the VS screen.
REQUIRED = {'INTRO.MOV': 'the Tekken 3 opening movie (-DT3_LOCAL_INTRO_MOVIE)',
            'VSBG0.BIN': 'the VS screen (-DT3_LOCAL_VS_RECORDS and -DT3_LOCAL_ARCADE_OVERLAY)',
            'MENU.BIN': 'the title and select menus', 'INTRO.BIN': 'the intro program'}


def missing_features(disc):
    """The REQUIRED files absent from the unpacked disc next to this cue, or None if it has no iso_root."""
    iso_root = disc.parent / 'iso_root'
    if not iso_root.is_dir():
        return None
    return [f'{name}: {what}' for name, what in REQUIRED.items() if not (iso_root / name).exists()]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc', type=Path, default=ROOT/'build/roster/disc/music.cue')
    p.add_argument('--mednafen', default='mednafen')
    p.add_argument('--force', action='store_true', help='play a disc even if it lacks the features')
    args = p.parse_args()
    missing = missing_features(args.disc)
    if missing and not args.force:
        sys.exit('this disc is not the full game, it lacks:\n  ' + '\n  '.join(missing)
                 + '\nreconfigure the build (scripts/print_release_configure.py) or pass --force')
    os.execvp(args.mednafen, [args.mednafen, '-sound', '1', '-ss.correct_aspect', '0', str(args.disc)])


if __name__ == '__main__':
    main()
