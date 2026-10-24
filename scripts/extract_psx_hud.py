#!/usr/bin/env python3
"""Extract original fight HUD pieces from the local USA PS1 executable.

Writes private VDP1 LUT4 patterns and RGB555 palettes, with zero transparent.
The source decoder and page/CLUT coordinates are from pinned Tekken3Remake
(PolyForm Noncommercial 1.0.0; see NOTICE.md). No game image enters git.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
sys.path.insert(0, str(RESEARCH))
import system_textures  # noqa: E402

# PS1 VRAM word positions and CLUT ids in hud.md and hud.py of the pinned remake.
PIECES = (
    ('bar_cap_left', 832, 0, 0xE0, 0xA8, 6, 24, 0x7EDE),
    ('bar_cap_right', 832, 0, 0xEA, 0xA8, 6, 24, 0x7EDE),
    ('bar_health', 832, 0, 0xE8, 0xA8, 2, 24, 0x7EDC),
    ('bar_damage', 832, 0, 0xE8, 0xA8, 2, 24, 0x7EDD),
    ('bar_empty', 832, 0, 0xE8, 0xA8, 2, 24, 0x7EDE),
    ('mark_won', 832, 0, 0xF0, 0xA8, 15, 18, 0x7F18),
    ('mark_empty', 832, 0, 0xF0, 0xA8, 15, 18, 0x7F19),
    *((f'digit_{d}', 896, 0, 16 * d, 0xD0, 16, 46, 0x7F1C) for d in range(12)),
)


def word(vram, x, y):
    if not 0 <= x < 1024 or not 0 <= y < 512:
        raise ValueError(f'VRAM coordinate outside 1024x512: {x},{y}')
    return struct.unpack_from('<H', vram, 2 * (1024 * y + x))[0]


def extract(vram, spec):
    name, page_x, page_y, u, v, width, height, clut = spec
    clut_x, clut_y = 16 * (clut & 63), (clut >> 6) & 511
    palette = [word(vram, clut_x + i, clut_y) for i in range(16)]
    if palette[0] != 0:
        raise ValueError(f'{name}: transparent palette entry is not zero')
    padded_width = (width + 7) & ~7
    pixels = bytearray()
    visible = 0
    for y in range(v, v + height):
        indices = []
        for x in range(u, u + width):
            packed = word(vram, page_x + x // 4, page_y + y)
            index = (packed >> (4 * (x & 3))) & 15
            visible += palette[index] != 0
            indices.append(index)
        # VDP1 requires an 8-texel width. Resample across that width so a
        # stretched 2-texel fill remains visible for the full destination.
        if padded_width != width:
            indices = [indices[col * width // padded_width] for col in range(padded_width)]
        pixels += bytes((indices[i] << 4) | indices[i + 1]
                        for i in range(0, padded_width, 2))
    # Saturn VDP1 LUT colours need the RGB bit set to mix with indexed sprites.
    lut = b''.join(struct.pack('<H', (value & 0x7FFF) | 0x8000 if value else 0)
                   for value in palette)
    return name, bytes(pixels), lut, dict(width=width, padded_width=padded_width,
                                          height=height, clut=clut, visible_pixels=visible)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/imported/tekken3-usa/hud')
    args = parser.parse_args()
    exe = args.exe.read_bytes()
    tims = system_textures.members(exe, 'usa')
    if len(tims) != 127:
        raise ValueError(f'expected 127 USA system texture members, got {len(tims)}')
    vram = system_textures.build_vram(tims)
    images = {}
    packed = bytearray(b'T3HUD01\0' + struct.pack('<HH', len(PIECES), 0))
    args.out.mkdir(parents=True, exist_ok=True)
    for spec in PIECES:
        name, pixels, lut, info = extract(vram, spec)
        if not info['visible_pixels']:
            raise ValueError(f'{name} has no visible texels')
        filename, palette_file = name + '.lut4', name + '.clut555'
        (args.out / filename).write_bytes(pixels)
        (args.out / palette_file).write_bytes(lut)
        # Retire the first extractor's per-pixel RGB output by exact name.
        (args.out / (name + '.rgb555')).unlink(missing_ok=True)
        images[name] = dict(file=filename, palette_file=palette_file,
                            sha256=hashlib.sha256(pixels).hexdigest(),
                            palette_sha256=hashlib.sha256(lut).hexdigest(), **info)
        packed += struct.pack('<HHHH', info['width'], info['padded_width'], info['height'], len(pixels))
        packed += lut + pixels
    (args.out / 'HUD.BIN').write_bytes(packed)
    manifest = dict(schema=1, source='Tekken 3 USA executable system textures',
                    exe_sha256=hashlib.sha256(exe).hexdigest(),
                    format='VDP1 LUT4, left texel in high nibble; RGB555 LE CLUT with bit15 visible',
                    disc_file='HUD.BIN', disc_sha256=hashlib.sha256(packed).hexdigest(),
                    images=images)
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{len(images)} HUD images -> {args.out}')


if __name__ == '__main__':
    main()
