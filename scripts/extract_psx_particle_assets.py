#!/usr/bin/env python3
"""Extract private LUT4 particle sprites, PS1 palettes and billboard geometry.

Coordinates/rules adapted from pinned Tekken3Remake effects.py and effects.md
(PolyForm Noncommercial; Copyright (c) 2026 n0madic; see NOTICE.md).
Preserves original STP bits separately from VDP1 RGB LUTs. No game data in Git.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from extract_psx_hud import ROOT, system_textures, word
from extract_psx_ai_tables import window_bytes

CLUTS = (*range(0x7DC6, 0x7DCB), *range(0x7F50, 0x7F60), *range(0x7F90, 0x7FA0))
# Same numeric model IDs as psx_spark.h, rather than the remake's enum.
MODELS = (0x8002554C, 0x8002593C, 0x8002556C, 0x800256E0, 0x80025700)


def extract(raw, release, segments=None):
    vram = system_textures.build_vram(system_textures.members(raw, release))
    sprites = []
    for v in (144, 128):
        indices = []
        for y in range(16):
            for x in range(16):
                u = 128 + x
                indices.append((word(vram, 960 + u // 4, 256 + v + y) >> (4 * (u & 3))) & 15)
        sprites.append(bytes((indices[i] << 4) | indices[i + 1] for i in range(0, 256, 2)))
    palettes = []
    for clut in CLUTS:
        values = [word(vram, 16 * (clut & 63) + i, clut >> 6) for i in range(16)]
        palettes.append(struct.pack('<16H', *values))
    models = []
    for address in MODELS:
        if segments is None:
            at = address - 0x80010000 + 0x800
            models.append(raw[at:at + 32])
        else:
            models.append(window_bytes(raw, segments, address, 32))
    return sprites, palettes, models


def pack(parts):
    sprites, palettes, models = parts
    out = bytearray(struct.pack('<8s4H', b'T3FX001\0', 2, len(CLUTS), len(MODELS), 16))
    out += b''.join(sprites)
    for clut, palette in zip(CLUTS, palettes):
        values = struct.unpack('<16H', palette)
        lut = struct.pack('<16H', *(0x8000 | (v & 0x7FFF) if v else 0 for v in values))
        out += struct.pack('<HH', clut, 0) + palette + lut
    out += b''.join(models)
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, default=ROOT/'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--rev1', type=Path)
    p.add_argument('--out', type=Path, default=ROOT/'assets/imported/tekken3-usa/particles')
    a = p.parse_args()
    raw = a.exe.read_bytes()
    layout = json.loads((ROOT/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json').read_text())['blocks']['exe']
    if hashlib.sha256(raw[0x800:]).hexdigest() != layout['sha256']:
        raise ValueError('unrecognized USA executable')
    parts = extract(raw, 'usa', layout['segments'])
    if a.rev1:
        jp = a.rev1.read_bytes()
        if hashlib.sha256(jp).hexdigest() != '09e8b0b6575c6ff1c81f7f827d31daf2770fa4c0a586e946b6f10f7613dd0432':
            raise ValueError('unrecognized Japan Rev.1 executable')
        if parts != extract(jp, 'jp_rev1'):
            raise ValueError('USA/Japan particle assets differ')
    data = pack(parts)
    report = dict(format='T3FX001', bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  exe_sha256=hashlib.sha256(raw).hexdigest(), rev1_identical=bool(a.rev1),
                  models=[dict(id=i+1, corners=[list(struct.unpack_from('<4h', model, j*8)) for j in range(4)])
                          for i, model in enumerate(parts[2])], palettes=[])
    for clut, palette in zip(CLUTS, parts[1]):
        values = struct.unpack('<16H', palette)
        report['palettes'].append(dict(clut=clut, index_zero_transparent=values[0] == 0,
                                      transparent=sum(v == 0 for v in values),
                                      stp=sum(bool(v & 0x8000) for v in values),
                                      opaque=sum(v != 0 and not v & 0x8000 for v in values)))
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out/'EFFECTS.BIN').write_bytes(data)
    (a.out/'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('format', 'bytes', 'sha256', 'rev1_identical')}))


if __name__ == '__main__':
    main()
