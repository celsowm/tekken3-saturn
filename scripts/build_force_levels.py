#!/usr/bin/env python3
"""Build one private disc file per Tekken Force level (LEVn.BIN, n = 1..4) from the stage archives.

The stage archives of the four levels (USA records 51-54, `stg_p..s`) hold four members
(docs/research/code/stages.md in Tekken3Remake): 0 the panorama tile TIMs, 1 the 808-byte floor
descriptor, 2 the tile map and 3 the 2,560-byte level script. force.ovl draws the map as 64 x 64
sprites (`FUN_800B4DF8`): a cell's texture word gives the texture page (`>> 8 & 0x1F`) and the tile's
u, v (`(w & 3) << 6`, `(w & 0xC) << 4`), the cell's CLUT word its 16 colours.

LEVn.BIN is big-endian: a 64-byte header, then the cells, the tiles, the palettes, the floor
descriptor and the script.

    header   u32 magic 'T3FL', version, level, map width, map height, tile count, palette count,
             cells offset, tiles offset, palettes offset, floor offset, script offset, script bytes
    cells    width * height of (u16 tile, u16 palette), row by row
    tiles    64 * 64 texels in 4 bits (left texel in the high nibble), 2,048 bytes each, once each
    palettes 16 u16 Saturn colours (0 stays transparent), once each
    floor    member 1 as stored; script member 3 as stored (little-endian records)

The output is retail-derived: keep it under assets/imported or build/.
"""
import argparse
from pathlib import Path
import struct

from psx_character_textures import saturn_colour, upload_block

MAGIC = 0x54334C46  # 'T3FL'
VERSION = 1
HEADER_BYTES = 64
TILE = 64
FIRST_RECORD = 51
LEVELS = 4


def read_stage(arc):
    """(members, vram) of a mode-8 stage archive: four members, member 0 the tile TIMs."""
    if len(arc) < 36 or struct.unpack_from('<I', arc)[0] != 4:
        raise ValueError('expected a four-member Tekken Force stage archive')
    members = []
    for i in range(4):
        start, size = struct.unpack_from('<II', arc, 4 + 8 * i)
        if start + size > len(arc):
            raise ValueError('stage archive member outside the file')
        members.append(arc[start:start + size])
    vram = [0] * (1024 * 512)
    tiles, count = members[0], struct.unpack_from('<I', members[0])[0]
    # Member 0 is a directory of `count` TIMs, one per tile; force.ovl FUN_800B4A88 uploads each
    # at the rectangles of its own header.
    if count == 0 or 4 + 8 * count > len(tiles):
        raise ValueError('stage tile directory does not fit its member')
    for i in range(count):
        start, size = struct.unpack_from('<II', tiles, 4 + 8 * i)
        tim = tiles[start:start + size]
        if len(tim) != size or struct.unpack_from('<II', tim) != (0x10, 8):
            raise ValueError('stage tiles must be 4-bit TIMs with a CLUT')
        at = upload_block(vram, tim, 8, 0, 0)
        upload_block(vram, tim, at, 0, 0)
    return members, vram


def tile_texels(vram, word):
    """The 64 x 64 texel indices of the map cell's tile (texture page 4-bit mode)."""
    page = word >> 8 & 0x1F
    x0, y0 = (page & 15) * 64, (page >> 4) * 256
    u, v = (word & 3) << 6, (word & 0xC) << 4
    rows = []
    for y in range(TILE):
        row = []
        for x in range(TILE):
            w = vram[(y0 + v + y) * 1024 + x0 + (u + x) // 4]
            row.append(w >> (4 * ((u + x) % 4)) & 15)
        rows.append(row)
    return rows


def build_level(arc, level):
    members, vram = read_stage(arc)
    map_member = members[2]
    width, height = struct.unpack_from('<HH', map_member)
    cells = width * height
    if len(map_member) != 4 + 4 * cells:
        raise ValueError(f'level {level}: tile map {width}x{height} does not fit its member')
    textures = struct.unpack_from(f'<{cells}H', map_member, 4)
    cluts = struct.unpack_from(f'<{cells}H', map_member, 4 + 2 * cells)
    tiles, palettes, packed_tiles, packed_palettes, rows = {}, {}, [], [], []
    for word, clut in zip(textures, cluts):
        texels = tile_texels(vram, word)
        key = tuple(map(tuple, texels))
        if key not in tiles:
            tiles[key] = len(tiles)
            packed_tiles.append(b''.join(
                bytes(row[x] << 4 | row[x + 1] for x in range(0, TILE, 2)) for row in texels))
        cx, cy = 16 * (clut & 63), clut >> 6
        colours = tuple(saturn_colour(vram[cy * 1024 + cx + i]) for i in range(16))
        if colours not in palettes:
            palettes[colours] = len(palettes)
            packed_palettes.append(struct.pack('>16H', *colours))
        rows.append((tiles[key], palettes[colours]))
    cell_bytes = b''.join(struct.pack('>HH', *r) for r in rows)
    tile_bytes, palette_bytes = b''.join(packed_tiles), b''.join(packed_palettes)
    floor, script = members[1], members[3]
    offsets, at = [], HEADER_BYTES
    for blob in (cell_bytes, tile_bytes, palette_bytes, floor, script):
        offsets.append(at)
        at += len(blob)
    header = struct.pack('>13I', MAGIC, VERSION, level, width, height, len(packed_tiles), len(packed_palettes),
                         *offsets, len(script))
    image = header.ljust(HEADER_BYTES, b'\0') + cell_bytes + tile_bytes + palette_bytes + floor + script
    return image, dict(level=level, file=f'LEV{level}.BIN', bytes=len(image), map=[width, height],
                       tiles=len(packed_tiles), palettes=len(packed_palettes), script_bytes=len(script))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--raw', type=Path, required=True, help='private directory of NNN.bin records')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for level in range(1, LEVELS + 1):
        image, row = build_level((args.raw / f'{FIRST_RECORD + level - 1:03}.bin').read_bytes(), level)
        (args.out / row['file']).write_bytes(image)
        print(row)


if __name__ == '__main__':
    main()
