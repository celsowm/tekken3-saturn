#!/usr/bin/env python3
"""Make the V0.5 itch.io cover (630x500) and screenshots from the showcase takes (scripts/edit_v05_video.py's inputs).

  cover.png            the wipe from V0.4's flat colours to V0.5's Gouraud lighting, with the release name
  gouraud-compare.png  the same frame of both discs side by side
  fight.png, ball.png, ball-select.png, practice.png   one frame each

    scripts/make_v05_itch_art.py --takes build/v05-video-takes --out build/itch-art-v05
"""
import argparse
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from edit_arcade_video import FONT
from release_version import version

# (name, take, seconds) of the single frames
STILLS = (('fight', 'gouraud-after', 9), ('ball', 'ball', 52), ('ball-select', 'ball', 18.2), ('practice', 'practice-menus', 9))
PAIR_AT = 4                    # seconds into both Gouraud takes
WIPE_AT = 5.2                  # seconds into both takes: the divider is about two thirds across


def frame(take, seconds, out):
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-ss', str(seconds), '-i', str(take), '-frames:v', '1', str(out)],
                   check=True)
    return Image.open(out).convert('RGB')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--takes', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for name, take, seconds in STILLS:
        frame(args.takes / f'{take}.mp4', seconds, args.out / f'{name}.png')
    before = frame(args.takes / 'gouraud-before.mp4', PAIR_AT, args.out / 'before.png')
    after = frame(args.takes / 'gouraud-after.mp4', PAIR_AT, args.out / 'after.png')
    pair = Image.new('RGB', (1280, 480))
    for n, image in enumerate((before, after)):
        pair.paste(image.resize((640, 480), Image.LANCZOS), (n * 640, 0))
    pair.save(args.out / 'gouraud-compare.png')
    # The cover: the left two thirds lit, the rest flat, a white divider, the release name under it.
    before, after = (frame(args.takes / f'gouraud-{k}.mp4', WIPE_AT, args.out / f'{k}.png') for k in ('before', 'after'))
    split = int(after.width * 0.62)
    wipe = before.copy()
    wipe.paste(after.crop((0, 0, split, after.height)), (0, 0))
    ImageDraw.Draw(wipe).rectangle((split - 2, 0, split + 2, wipe.height), fill=(255, 255, 255))
    cover = Image.new('RGB', (630, 500), (14, 14, 14))
    cover.paste(wipe.resize((630, 472), Image.LANCZOS).crop((0, 0, 630, 410)), (0, 0))
    draw = ImageDraw.Draw(cover)
    draw.text((16, 424), f'TEKKEN 3 for the Sega Saturn  {version()}', font=ImageFont.truetype(FONT, 26), fill=(255, 255, 255))
    draw.text((16, 460), 'lit fighters, Tekken Ball, Practice', font=ImageFont.truetype(FONT, 20), fill=(120, 230, 255))
    cover.save(args.out / 'cover.png')
    for name in ('before', 'after'):
        (args.out / f'{name}.png').unlink()
    print(sorted(path.name for path in args.out.iterdir()))


if __name__ == '__main__':
    main()
