#!/usr/bin/env python3
"""Extract the fire, smoke, cloud and gas flipbooks of the effect pool (types 6-11, 19, 20) as FIRE.BIN.

Every pool object takes its frame from effect page 1 at VRAM (960,256), 64 words x 256 lines of 4-bit texels, with a
(u, v) byte pair per frame from the executable's tables (docs/PARTICLE_INTEGRATION_PLAN.md section 39; the remake's
effects.md "Drawing by type and phase"). The frames are 32 x 32 or 16 x 16 texels. FIRE.BIN holds, little endian:

    header   'T3FR001\\0', u16 cells, u16 frames, u16 models, u16 reserved
    5 palettes of 16 PS1 words (CLUT ids 0x7DC6..0x7DCA, at (96 + 16 i, 503))
    models   32 bytes each, four SVECTORs (x, y, z, pad) as the original's model tables, FIRE_MODELS order
    frames   one cell index per frame id (id 1 is entry 0), in FRAME_TABLES order
    sizes    one byte per cell: 32 or 16
    cells    packed two texels to a byte, high nibble first (512 / 128 bytes)

CLUTs 0x7DC8 and 0x7DC9 draw their index 0 (white) and use index 15 or 14 for the transparent black. VDP1 treats code 0
as transparent, so the cells that only the fire palettes use (v >= 160) are stored with texel values 0 and 15 swapped;
the runtime swaps the same two entries of those palettes. Private output; nothing is committed. `--check DUMP` compares
the cells and palettes with a PS1 VRAM dump (1 MiB, 1024 x 512 little-endian words).
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from extract_psx_ai_tables import window_bytes
from extract_psx_hud import ROOT, system_textures, word
from extract_psx_sound_tables import BASE, HEADER

PAGE_X, PAGE_Y = 960, 256
CLUT_X, CLUT_Y, CLUTS = 96, 503, 5
FIRE_CELL_V = 160
# (name, address, frames, size): frame ids run in this order, from 1
FRAME_TABLES = (
    ('fireball_grow', 0x80025760, 8, 32), ('fireball_loop', 0x80025770, 8, 32), ('fireball_burst', 0x80025780, 8, 32),
    ('smoke_puff', 0x80025798, 8, 16), ('smoke_fade', 0x800257A8, 12, 16), ('flame_grow', 0x80025820, 4, 32),
    ('flame_loop', 0x80025828, 4, 32), ('flame_burst', 0x80025830, 5, 32), ('fire_fade', 0x80025858, 16, 16),
    ('cloud', 0x800258B8, 21, 32), ('gas', 0x80025A0C, 27, 32))
# the objects' models (half-size quads): fireball A and B, smoke, flame A and B, floor fire, cloud, burn, Gon's burn, gas A and B
FIRE_MODELS = (0x80025740, 0x80025720, 0x800257C0, 0x800257E0, 0x80025800, 0x80025878, 0x80025898,
               0x800258FC, 0x8002591C, 0x800259EC, 0x800259CC)


def swapped(u, v, size, table):
    return v >= FIRE_CELL_V and size == 32 and not table.startswith('cloud') and not table.startswith('gas')


def cell_texels(read, u, v, size):
    out = []
    for y in range(size):
        for x in range(size):
            w = read(PAGE_X + ((u + x) >> 2), PAGE_Y + v + y)
            out.append((w >> (4 * ((u + x) & 3))) & 15)
    return out


def swap_fire(t):
    return 15 if t == 0 else 0 if t == 15 else t


def extract(read, raw, segments):
    cells, frames, order = [], [], {}
    for name, address, count, size in FRAME_TABLES:
        b = window_bytes(raw, segments, address, 2 * count)
        for i in range(count):
            u, v = b[2 * i], b[2 * i + 1]
            key = (u, v, size, swapped(u, v, size, name))
            if key not in order:
                order[key] = len(cells)
                cells.append(key)
            frames.append(order[key])
    out = bytearray(struct.pack('<8s4H', b'T3FR001\0', len(cells), len(frames), len(FIRE_MODELS), 0))
    palettes = [[read(CLUT_X + 16 * c + i, CLUT_Y) for i in range(16)] for c in range(CLUTS)]
    for p in palettes:
        out += struct.pack('<16H', *p)
    for address in FIRE_MODELS:
        out += bytes(window_bytes(raw, segments, address, 32))
    out += bytes(frames) + bytes(c[2] for c in cells)
    texels = []
    for u, v, size, swap in cells:
        t = cell_texels(read, u, v, size)
        if swap:
            t = [swap_fire(x) for x in t]
        texels.append(t)
        out += bytes((t[i] << 4) | t[i + 1] for i in range(0, len(t), 2))
    return bytes(out), texels, palettes


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--out', type=Path, default=ROOT / 'build/generated/private_ball')
    p.add_argument('--check', type=Path, help='PS1 VRAM dump to compare against')
    a = p.parse_args()
    out = a.out.resolve()
    if not any(root.resolve() in out.parents or root.resolve() == out for root in (ROOT / 'assets/imported', ROOT / 'build')):
        p.error('output must be under assets/imported or build')
    raw = a.exe.read_bytes()
    layout = json.loads((ROOT / 'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json').read_text())['blocks']['exe']
    if hashlib.sha256(raw[HEADER:]).hexdigest() != layout['sha256']:
        sys.exit('unrecognized USA executable')
    vram = system_textures.build_vram(system_textures.members(raw, 'usa'))
    data, texels, palettes = extract(lambda x, y: word(vram, x, y), raw, layout['segments'])
    report = dict(format='T3FR001', bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  cells=len(texels), exe_sha256=hashlib.sha256(raw).hexdigest())
    if a.check:
        dump = a.check.read_bytes()
        if len(dump) != 1024 * 512 * 2:
            sys.exit('not a 1 MiB VRAM dump')
        _, ref_texels, ref_palettes = extract(lambda x, y: struct.unpack_from('<H', dump, (y * 1024 + x) * 2)[0], raw, layout['segments'])
        bad = sum(1 for c in range(len(texels)) if texels[c] != ref_texels[c]) + (palettes != ref_palettes)
        report['checked_against_ps1_vram'] = bad == 0
        if bad:
            print(json.dumps(report))
            sys.exit('extracted cells differ from the PS1 dump in %d places' % bad)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'FIRE.BIN').write_bytes(data)
    print(json.dumps(report))


if __name__ == '__main__':
    main()
