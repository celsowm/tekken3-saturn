#!/usr/bin/env python3
"""Write KEYS.BIN, the three Tekken Force key sprites (copper, silver, gold), from force.ovl.

force.ovl keeps an ARC of three 4-bit TIMs at 0x800B0FE0 (Japan Rev.1; `FUN_800B4374`'s key
icons use them). KEYS.BIN is big-endian: u32 count, then per key `u16 width, u16 height` in texels,
16 u16 Saturn colours (0 transparent) and the texels in 4 bits, left texel in the high nibble.
The input is the overlay record (Tekken3Remake work/jp_rev1/bns/007_rec, or the BNS record 7 of
the disc). The output is retail-derived: keep it under assets/imported or build/.
"""
import argparse
from pathlib import Path
import struct

from psx_character_textures import saturn_colour

BASE, ARC = 0x800B0A10, 0x800B0FE0


def key_sprite(ovl, start):
    magic, flags = struct.unpack_from('<II', ovl, start)
    if (magic, flags) != (0x10, 8):
        raise ValueError('key sprite is not a 4-bit TIM with a CLUT')
    at = start + 8
    clut_len = struct.unpack_from('<I', ovl, at)[0]
    colours = struct.unpack_from('<16H', ovl, at + 12)
    at += clut_len
    length, _, _, words, height = struct.unpack_from('<I4H', ovl, at)
    data = ovl[at + 12:at + length]
    width = words * 4
    texels = bytearray()
    # PS1 4-bit rows: each halfword holds four texels, the lowest nibble is the left one.
    for row in range(height):
        pixels = []
        for w in struct.unpack_from(f'<{words}H', data, row * words * 2):
            pixels += [w & 15, w >> 4 & 15, w >> 8 & 15, w >> 12 & 15]
        texels += bytes(pixels[i] << 4 | pixels[i + 1] for i in range(0, width, 2))
    return struct.pack('>HH', width, height) + struct.pack('>16H', *map(saturn_colour, colours)) + bytes(texels)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--overlay', type=Path, required=True, help="force.ovl's BNS record (record 7)")
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    ovl = args.overlay.read_bytes()
    at = ARC - BASE
    count = struct.unpack_from('<I', ovl, at)[0]
    if count != 3:
        raise ValueError(f'expected three key sprites, found {count}')
    out = struct.pack('>I', count)
    for i in range(count):
        start, _ = struct.unpack_from('<II', ovl, at + 4 + 8 * i)
        out += key_sprite(ovl, at + start)
    args.out.write_bytes(out)
    print(f'{len(out)} bytes -> {args.out}')


if __name__ == '__main__':
    main()
