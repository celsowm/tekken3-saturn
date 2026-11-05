#!/usr/bin/env python3
"""Cut the V0.3 arcade-mode showcase from three Mednafen takes (scripts/record_reddit_video.py).

  win    a real ladder run: intro, title, select, VS, stage 1 fight  (--ladder, fight inputs)
  loss   the same run with an idle pad: YOU LOSE, CONTINUE?, GAME OVER, the title
  late   a -DT3_LADDER_SELFTEST=ON build starting at fight 9: stage 9, Ogre, True Ogre, the ending movie

The loading screens between them are black; each take's span list skips them. A card says that the last part
comes from a test build that wins its fights by itself, and an end card carries the links. Output: --out
(full cut) and --short (the same story in 2:20 or less, X's limit for a standard account).
Spans were read from the takes with blackdetect and a few screenshots; a new take needs new spans.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 960, 720
FONT = '/System/Library/Fonts/Helvetica.ttc'
LINKS = ['Download: celsowm.itch.io/tekken-3-saturn-demo', 'Game code: github.com/celsowm/tekken3-saturn',
         'Built on LibSaturn: github.com/celsowm/libsaturn', 'Unofficial fan project, not affiliated with Bandai Namco']

# (take, start, end) in seconds of the take.
FULL = [('win', 0, 31.4), ('win', 42, 50.2), ('win', 86, 110), ('win', 137.3, 236.3),
        ('loss', 224, 251), ('loss', 261, 266),
        'CARD_TEST',
        ('late', 105, 112), ('late', 138.2, 143.9), ('late', 186, 194), ('late', 216.8, 222.6),
        ('late', 264.5, 272), ('late', 294.6, 337.4)]
SHORT = [('win', 0, 12), ('win', 44, 50.2), ('win', 90, 108), ('win', 137.3, 163),
         ('loss', 232, 251), ('loss', 261, 265),
         'CARD_TEST',
         ('late', 186, 190), ('late', 216.8, 222.6), ('late', 264.5, 268), ('late', 294.6, 316)]


def card(path, lines, size=44, tint=(255, 255, 255)):
    image = Image.new('RGB', (W, H), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, size)
    total = len(lines) * (size + 22)
    y = (H - total) // 2
    for line in lines:
        width = draw.textlength(line, font=font)
        draw.text(((W - width) / 2, y), line, font=font, fill=tint)
        y += size + 22
    image.save(path)


def encode(args, out):
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', *args, '-vf', f'scale={W}:{H}:flags=lanczos,setsar=1,fps=60',
                    '-c:v', 'libx264', '-crf', '20', '-preset', 'medium', '-pix_fmt', 'yuv420p',
                    '-c:a', 'aac', '-b:a', '160k', '-ar', '44100', '-ac', '2', str(out)], check=True)


def build(spec, takes, work, out):
    parts = []
    for n, item in enumerate(spec):
        part = work / f'{out.stem}-{n:02d}.mp4'
        if item == 'CARD_TEST':
            png = work / 'test.png'
            card(png, ['Test build: it wins each fight by itself,', 'so the last stages are quick to show.',
                       'Same game and disc code.'], 40, (255, 210, 80))
            encode(['-loop', '1', '-t', '4', '-i', png, '-f', 'lavfi', '-t', '4', '-i', 'anullsrc=r=44100:cl=stereo',
                    '-shortest'], part)
        else:
            take, start, end = item
            fade = f'afade=t=in:d=0.04,afade=t=out:st={end - start - 0.04:.3f}:d=0.04'
            encode(['-ss', str(start), '-to', str(end), '-i', takes[take], '-af', fade], part)
        parts.append(part)
    intro, final = work / f'{out.stem}-title.mp4', work / f'{out.stem}-links.mp4'
    for part, lines, seconds, size in ((intro, ['TEKKEN 3 for the Sega Saturn', 'V0.3: real Arcade Mode',
                                                '21 fighters, 14 stages'], 3, 52),
                                       (final, LINKS, 7, 34)):
        png = work / (part.stem + '.png')
        card(png, lines, size)
        encode(['-loop', '1', '-t', str(seconds), '-i', png, '-f', 'lavfi', '-t', str(seconds), '-i',
                'anullsrc=r=44100:cl=stereo', '-shortest'], part)
    parts = [intro] + parts + [final]
    listing = work / f'{out.stem}.txt'
    listing.write_text(''.join(f"file '{p}'\n" for p in parts))
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', listing, '-c', 'copy',
                    '-movflags', '+faststart', str(out)], check=True)
    seconds = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                                             '-of', 'csv=p=0', str(out)]))
    print(f'{out}: {seconds:.1f} s, {out.stat().st_size / 1048576:.1f} MiB')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ('win', 'loss', 'late'):
        p.add_argument(f'--{name}', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--short', type=Path, required=True)
    args = p.parse_args()
    takes = {'win': args.win, 'loss': args.loss, 'late': args.late}
    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        build(FULL, takes, work, args.out)
        build(SHORT, takes, work, args.short)


if __name__ == '__main__':
    main()
