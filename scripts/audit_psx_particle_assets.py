#!/usr/bin/env python3
"""Audit private particle assets using the remake's independent canonical reader.

Read-only: checks every texel, PS1/STP palette word, converted LUT and model.
Uses pinned Tekken3Remake common/effects/vram (PolyForm Noncommercial;
Copyright (c) 2026 n0madic; see NOTICE.md). Run with build/asset-venv Python.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'third_party/Tekken3Remake/tools/remake_import'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc', type=Path, required=True)
    p.add_argument('--assets', type=Path, required=True)
    a = p.parse_args()
    from common import Disc
    from effects import OBJECT_CLUTS
    source = Disc.open(a.disc)
    try:
        disc = source.canonical()
        vram = disc.system_vram()
        data = a.assets.read_bytes()
        if struct.unpack_from('<8s4H', data) != (b'T3FX001\0', 2, 37, 5, 16):
            raise ValueError('particle asset header mismatch')
        at = 16
        for y in (144, 128):
            texels = vram.page_indices(960, 256, 4)[y:y+16, 128:144].reshape(-1)
            expected = bytes((int(texels[i]) << 4) | int(texels[i+1]) for i in range(0, 256, 2))
            if data[at:at+128] != expected:
                raise ValueError(f'particle texels mismatch at v={y}')
            at += 128
        for clut in OBJECT_CLUTS:
            values = [int(v) for v in vram.clut(16*(clut & 63), clut >> 6, 16)]
            expected = (struct.pack('<HH16H', clut, 0, *values) +
                        struct.pack('<16H', *(0x8000 | (v & 0x7FFF) if v else 0 for v in values)))
            if data[at:at+68] != expected:
                raise ValueError(f'particle palette mismatch: {clut:#x}')
            at += 68
        for address in (0x8002554C, 0x8002593C, 0x8002556C, 0x800256E0, 0x80025700):
            if data[at:at+32] != struct.pack('<16h', *disc.exe_s16(address, 16)):
                raise ValueError(f'particle model mismatch: {address:#x}')
            at += 32
        if at != len(data):
            raise ValueError('particle assets have trailing bytes')
        print(json.dumps(dict(passed=True, independent_reader='canonical Disc + Vram',
                              sprites=2, texels=512, palettes=37, ps1_palette_words=592,
                              vdp1_palette_words=592, models=5, model_words=80, bytes=at,
                              sha256=hashlib.sha256(data).hexdigest())))
    finally:
        source.image.close()


if __name__ == '__main__':
    main()
