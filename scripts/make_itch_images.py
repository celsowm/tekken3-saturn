#!/usr/bin/env python3
"""Make the itch.io cover (630x500) and screenshots from emulator captures. Output: build/itch-art/ (git-ignored)."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/itch-art'
SHOTS = {'title': 'build/intro-run/i6300.png', 'select': 'build/menu-flow/select.png', 'fight': 'build/menu-flow/fight.png'}


def load(name):
    image = Image.open(ROOT / SHOTS[name]).convert('RGB')
    return image.resize((640, 448), Image.NEAREST) if image.width < 400 else image


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name in SHOTS:
        load(name).save(OUT / f'{name}.png')
    cover = Image.new('RGB', (630, 500), (24, 24, 24))
    title = load('title')
    title = title.crop((40, 20, 664, 410)).resize((630, 394), Image.LANCZOS)
    cover.paste(title, (0, 53))
    cover.save(OUT / 'cover.png')
    print(sorted(p.name for p in OUT.iterdir()))


if __name__ == '__main__':
    main()
