#!/usr/bin/env python3
"""Pack the Tekken Force result screen's background from the local USA disc.

Input (private, from scripts/psx_disc_import.py --extract-ids 302): result.ovl (record 302).
The rules follow Tekken3Remake (n0madic, PolyForm Noncommercial 1.0.0; see NOTICE.md),
docs/research/code/modes.md (Result screens) and tools/research/draw_sim.py (result_picture):
the screen is a 368 x 480 picture, the Tekken 3 logo on red lightning, stored in the overlay as a
six-member TIM archive (Rev.1 0x800D8A20, USA 0x800D8400). Each member is a 128-wide 8-bit
piece at its own VRAM rectangle (x 512..703, y 0..479); the right column shows 112 of its 128.
Output is private and never enters git.

  FORCEBG.BIN   'T3BG0001' as in pack_menu_art.py, at the menu's 352 x 448.
"""
import argparse
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
sys.path.insert(0, str(ROOT / 'scripts'))
import overlay_images  # noqa: E402
import pack_menu_art as menu  # noqa: E402
import system_textures  # noqa: E402

USA_SCREEN_OVERLAY = 0x800B8D58
FORCE_PICTURE = 0x800D8400          # USA; Rev.1 0x800D8A20


def force_picture(record, tmp):
    """The 368 x 480 picture, each member pasted at its VRAM origin."""
    data = Path(record).read_bytes()
    found = [f for f in overlay_images.scan(data, USA_SCREEN_OVERLAY)
             if f.archive and len(f.tims) == 6 and getattr(f, 'address', FORCE_PICTURE) == FORCE_PICTURE]
    if len(found) != 1:
        raise SystemExit(f'{record}: expected one six-member archive at {FORCE_PICTURE:#x}')
    picture = Image.new('RGB', (384, 480))
    for tim in found[0].tims:
        image = {b[0]: b for b in system_textures.tim_blocks(tim)}['image']
        x, y = (image[1] - 512) * 2, image[2]
        picture.paste(menu.tim_image(tim, tmp).convert('RGB'), (x, y))
    return picture.crop((0, 0, 368, 480))


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--result', type=Path, required=True, help='result.ovl (USA record 302)')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        picture = force_picture(a.result, tmp)
    menu.background(picture.resize((menu.W, menu.H), Image.Resampling.LANCZOS), 'FORCEBG.BIN', a.out)
    picture.save(a.out / 'force-result-bg.png')


if __name__ == '__main__':
    main()
