#!/usr/bin/env python3
"""Cut the V0.4 Tekken Force showcase from one Ymir take (scripts/record_ymir_video.py).

  take   the full disc: title, mode list to TEKKEN FORCE MODE, the select, NOW LOADING, level 1 and its Crows

The take's input is a seeded pad script, so a card says so; an end card carries the links. The loading gaps and
the empty start of the level are skipped. Spans were read from the take (--seed 5 --down 5 --right 2 --pick-at 3300
--frames 9500) with a few screenshots; a new take needs new spans.
"""
import argparse
import tempfile
from pathlib import Path

from edit_arcade_video import LINKS, card, encode
import subprocess

# (start, end) in seconds of the take.
SPANS = [(36, 41.5), (52.5, 59), (64, 70), (77, 90), (104, 133.6)]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--take', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        parts = []
        for name, lines, seconds, size in (('title', ['TEKKEN 3 for the Sega Saturn', 'V0.4: Tekken Force'], 3, 52),
                                           ('note', ['Recorded in the Ymir emulator.',
                                                     'The pad input is a scripted test run.'], 3, 40)):
            png, part = work / f'{name}.png', work / f'{name}.mp4'
            card(png, lines, size, (255, 255, 255) if name == 'title' else (255, 210, 80))
            encode(['-loop', '1', '-t', str(seconds), '-i', png, '-f', 'lavfi', '-t', str(seconds), '-i',
                    'anullsrc=r=44100:cl=stereo', '-shortest'], part)
            parts.append(part)
        for n, (start, end) in enumerate(SPANS):
            part = work / f'span-{n:02d}.mp4'
            fade = f'afade=t=in:d=0.04,afade=t=out:st={end - start - 0.04:.3f}:d=0.04'
            encode(['-ss', str(start), '-to', str(end), '-i', args.take, '-af', fade], part)
            parts.append(part)
        png, final = work / 'links.png', work / 'links.mp4'
        card(png, LINKS, 34)
        encode(['-loop', '1', '-t', '7', '-i', png, '-f', 'lavfi', '-t', '7', '-i', 'anullsrc=r=44100:cl=stereo',
                '-shortest'], final)
        parts.append(final)
        listing = work / 'parts.txt'
        listing.write_text(''.join(f"file '{p}'\n" for p in parts))
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', listing, '-c', 'copy',
                        '-movflags', '+faststart', str(args.out)], check=True)
    seconds = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                                             '-of', 'csv=p=0', str(args.out)]))
    print(f'{args.out}: {seconds:.1f} s, {args.out.stat().st_size / 1048576:.1f} MiB')


if __name__ == '__main__':
    main()
