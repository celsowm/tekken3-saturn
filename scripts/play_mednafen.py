#!/usr/bin/env python3
"""Open a built disc in Mednafen to play it, with sound and the whole picture.

The title and select run 704 wide. With ss.correct_aspect on, Mednafen keeps
the dots where they are and cuts the picture at 660, so the last 44 dots on the
right are lost and centred art looks shifted right. Both settings are passed
for this run only.
"""
import argparse
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc', type=Path, default=ROOT/'build/roster/disc/music.cue')
    p.add_argument('--mednafen', default='mednafen')
    args = p.parse_args()
    os.execvp(args.mednafen, [args.mednafen, '-sound', '1', '-ss.correct_aspect', '0', str(args.disc)])


if __name__ == '__main__':
    main()
