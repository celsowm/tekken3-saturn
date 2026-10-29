#!/usr/bin/env python3
"""Check packed private HUD glyphs against the pinned canonical USA converter.

Reads canonical-rebased font descriptors and upstream VRAM texels/palettes,
independently of the exporter's USA offsets. Does not write game assets.
Run with the research venv (numpy). Output contains counts and hashes only.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/remake_import'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--hud-dir', type=Path, required=True)
    args = parser.parse_args()
    import numpy as np
    from common import Disc
    from hud import FONT_TABLE
    from vram import tpage_xy

    source = Disc.open(args.disc)
    try:
        disc = source.canonical()
        display = disc.exe_u16(0x800B1190, 4)
        if display != [0, 20, 368, 448]:
            raise AssertionError(f'unexpected original 2D display rectangle: {display}')
        vram = disc.system_vram()
        manifest = json.loads((args.hud_dir / 'manifest.json').read_text())
        count = 0
        for name, glyph in manifest['images'].items():
            if not name.startswith(('glyph_', 'replay_')):
                continue
            font = glyph['font']
            at = FONT_TABLE + 16 * font
            table = disc.exe_u32(at, 1)[0]
            _, _, width, height, tpage = disc.exe_u16(at + 4, 5)
            uv = disc.exe_u16(table + 2 * ord(glyph['char']), 1)[0]
            page = tpage_xy(tpage)
            raw = np.frombuffer((args.hud_dir / glyph['file']).read_bytes(), dtype=np.uint8)
            indices = np.column_stack((raw >> 4, raw & 15)).reshape(height, glyph['padded_width'])
            original = vram.page_indices(*page, 4)[uv >> 8:(uv >> 8) + height,
                                                       uv & 255:(uv & 255) + width]
            if (width, height) != (glyph['width'], glyph['height']) or \
                    not np.array_equal(indices[:, :width], original) or np.any(indices[:, width:]):
                raise AssertionError(f'{name}: font metrics, texels or padding differ')
            count += 1
        for key, palette in manifest['text_palettes'].items():
            font, colour = map(int, key.split(':'))
            offset = disc.exe_u8(FONT_TABLE + 16 * font + 14, 1)[0]
            k = colour + offset
            original = vram.clut(256 + 16 * (k & 15), ((k >> 4) + 24) | 480, 16)
            expected = b''.join(struct.pack('<H', (int(v) & 0x7FFF) | 0x8000 if v else 0)
                                for v in original)
            if (args.hud_dir / palette['palette_file']).read_bytes() != expected:
                raise AssertionError(f'{key}: palette differs')
        packed = (args.hud_dir / 'HUD.BIN').read_bytes()
        if packed[:8] != b'T3HUD04\0' or struct.unpack_from('<HH', packed, 8) != \
                (len(manifest['images']), len(manifest['text_palettes'])):
            raise AssertionError('packed HUD header differs from its manifest')
        offset = 12
        for name, image in manifest['images'].items():
            pixels = (args.hud_dir / image['file']).read_bytes()
            lut = (args.hud_dir / image['palette_file']).read_bytes()
            record = struct.pack('<HHHH', image['width'], image['padded_width'], image['height'], len(pixels))
            expected = record + lut + pixels
            if packed[offset:offset + len(expected)] != expected:
                raise AssertionError(f'{name}: packed record differs from audited asset')
            offset += len(expected)
        for palette in manifest['text_palettes'].values():
            lut = (args.hud_dir / palette['palette_file']).read_bytes()
            if packed[offset:offset + len(lut)] != lut:
                raise AssertionError('packed message palette differs from audited asset')
            offset += len(lut)
        if offset != len(packed):
            raise AssertionError('packed HUD has trailing bytes')
        print(json.dumps({'display_rectangle': display,
                          'glyphs_matched': count, 'palettes_matched': len(manifest['text_palettes']),
                          'hud_sha256': hashlib.sha256(packed).hexdigest(), 'bytes': len(packed)}, indent=2))
    finally:
        source.image.close()


if __name__ == '__main__':
    main()
