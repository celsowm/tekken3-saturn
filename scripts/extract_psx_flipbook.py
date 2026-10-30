#!/usr/bin/env python3
"""Extract the hit-burst flipbook (set 3, index 0) as indexed8 frames and a palette.

The frames are 32x32 4-bit texels in the system effect page 2 at VRAM (896,256),
eight per row, 8 words (32 texels) wide and 32 lines tall, 27 frames, with the
16-colour palette at CLUT (32,503). They are written packed two texels to a byte (high nibble first, 512 bytes a frame, the
resident size that fits High WRAM) plus the PS1 palette words; the runtime expands one
frame to indexed8, the format of the SDK's VDP2 sprite add mode, when it draws it. Private output;
nothing is committed. `--check DUMP` compares the extracted frames and palette with a
PS1 VRAM dump (1 MiB, 1024x512 little-endian words) texel for texel.

Layout read from FUN_8007699C / FUN_80077158 (see docs/PARTICLE_INTEGRATION_PLAN.md,
sections 46 and 48) and the page geometry of system-textures.md."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from extract_psx_hud import ROOT, system_textures, word

PAGE_X, PAGE_Y = 896, 256
FRAME_WORDS, FRAME_LINES, PER_ROW, FRAMES = 8, 32, 8, 27
CLUT_X, CLUT_Y = 32, 503


def frame_texels(read, f):
    """Texels of frame f (0-based) as 32 rows of 32 indices."""
    col, row = f % PER_ROW, f // PER_ROW
    rows = []
    for y in range(FRAME_LINES):
        line = []
        for wx in range(FRAME_WORDS):
            w = read(PAGE_X + col * FRAME_WORDS + wx, PAGE_Y + row * FRAME_LINES + y)
            line += [(w >> (4 * k)) & 15 for k in range(4)]
        rows.append(line)
    return rows


def extract(read):
    frames = [frame_texels(read, f) for f in range(FRAMES)]
    palette = [read(CLUT_X + i, CLUT_Y) for i in range(16)]
    out = bytearray(struct.pack('<8s4H', b'T3FB002\0', FRAMES, 32, 32, 16))
    out += struct.pack('<16H', *palette)
    for frame in frames:
        flat = [t for line in frame for t in line]
        out += bytes((flat[i] << 4) | flat[i + 1] for i in range(0, len(flat), 2))
    return bytes(out), frames, palette


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--out', type=Path, default=ROOT / 'assets/imported/tekken3-usa/flipbook')
    p.add_argument('--check', type=Path, help='PS1 VRAM dump to compare against')
    a = p.parse_args()
    raw = a.exe.read_bytes()
    vram = system_textures.build_vram(system_textures.members(raw, 'usa'))
    data, frames, palette = extract(lambda x, y: word(vram, x, y))
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / 'FLIPBOOK.BIN').write_bytes(data)
    report = dict(format='T3FB002', frames=FRAMES, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  exe_sha256=hashlib.sha256(raw).hexdigest(), palette=[hex(v) for v in palette])
    if a.check:
        dump = a.check.read_bytes()
        if len(dump) != 1024 * 512 * 2:
            sys.exit('not a 1 MiB VRAM dump')
        _, ref_frames, ref_palette = extract(lambda x, y: struct.unpack_from('<H', dump, (y * 1024 + x) * 2)[0])
        bad = sum(1 for f in range(FRAMES) if frames[f] != ref_frames[f]) + (palette != ref_palette)
        report['checked_against_ps1_vram'] = bad == 0
        if bad:
            print(json.dumps(report))
            sys.exit('extracted frames differ from the PS1 dump in %d places' % bad)
    (a.out / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
