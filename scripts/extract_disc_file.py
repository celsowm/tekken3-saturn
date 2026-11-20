#!/usr/bin/env python3
"""Extract one file from the data track of a mixed-mode Saturn disc (music.cue + music.bin).

The data track is MODE1/2352: the 2048 bytes of user data start 16 bytes into each sector. They are
rewritten as a plain ISO next to the output, read with bsdtar, and the ISO is deleted. Used to get a
release's boot program (0.BIN, the same bytes as build/<dir>/tekken3_saturn.app.bin) out of a packaged zip:

    scripts/extract_disc_file.py --cue build/v05-video-before/music.cue --name 0.BIN --out build/v05-video-before/0.BIN
"""
import argparse
import re
import subprocess
from pathlib import Path

SECTOR, DATA = 2352, 2048


def data_sectors(cue_text):
    """Sectors of track 1: up to the pregap (INDEX 00) of track 2, else up to INDEX 01 of track 2."""
    track2 = cue_text.split('TRACK 02', 1)[1]
    found = re.search(r'INDEX 0[01] (\d+):(\d+):(\d+)', track2)
    minutes, seconds, frames = map(int, found.groups())
    return (minutes * 60 + seconds) * 75 + frames


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--cue', type=Path, required=True)
    p.add_argument('--name', required=True, help='file name in the ISO root')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    cue_text = args.cue.read_text()
    binary = args.cue.parent / re.search(r'FILE "([^"]+)"', cue_text).group(1)
    sectors = data_sectors(cue_text)
    iso = args.out.with_suffix('.iso.tmp')
    try:
        with binary.open('rb') as source, iso.open('wb') as target:
            for _ in range(sectors):
                block = source.read(SECTOR)
                target.write(block[16:16 + DATA])
        with args.out.open('wb') as out:
            subprocess.run(['bsdtar', '-xOf', str(iso), args.name], stdout=out, check=True)
    finally:
        iso.unlink(missing_ok=True)
    print(f'{args.out}: {args.out.stat().st_size} bytes')


if __name__ == '__main__':
    main()
