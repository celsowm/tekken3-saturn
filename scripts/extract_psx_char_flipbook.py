#!/usr/bin/env python3
"""Extract a character's hit-effect flipbook (member 1 of its ARC) in the T3FB002 format.

Member 1 is 22 or 30 standard 4-bit TIMs of 32x32 texels, each with a 16-entry
CLUT (docs: Tekken3Remake system-textures.md, "Character effect flipbooks").
The output is the same layout as extract_psx_flipbook.py: header, the 16 PS1
palette words, then each frame packed two texels to a byte (leftmost in the high
nibble, 512 bytes a frame). All frames must share one CLUT, apart from the STP bit (bit 15) that frame 0 sets differently;
the palette of frame 1 is written. Private output;
nothing is committed."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

FRAME_BYTES = 512


def arc_member(arc, index):
    offset, size = struct.unpack_from('<II', arc, 4 + 8 * index)
    return arc[offset:offset + size]


def tims(member):
    """Yield (palette words, 32x32 texel bytes) for each TIM of the sequence."""
    o = 0
    while o < len(member):
        magic, flags = struct.unpack_from('<II', member, o)
        if magic != 0x10 or flags != 8:
            sys.exit('not a 4-bit TIM with a CLUT at %d' % o)
        p = o + 8
        size, _, _, w, h = struct.unpack_from('<IHHHH', member, p)
        if (w, h) != (16, 1):
            sys.exit('unexpected CLUT size %dx%d' % (w, h))
        palette = struct.unpack_from('<16H', member, p + 12)
        p += size
        size, _, _, w, h = struct.unpack_from('<IHHHH', member, p)
        if (w, h) != (8, 32) or size != 12 + FRAME_BYTES:
            sys.exit('unexpected image size %dx%d words' % (w, h))
        data = member[p + 12:p + 12 + FRAME_BYTES]
        # PS1 words hold four texels, lowest nibble first; repack with the leftmost in the high nibble.
        out = bytearray()
        for i in range(0, FRAME_BYTES, 2):
            word = data[i] | (data[i + 1] << 8)
            t = [(word >> (4 * k)) & 15 for k in range(4)]
            out += bytes(((t[0] << 4) | t[1], (t[2] << 4) | t[3]))
        yield palette, bytes(out)
        o = p + size


def flipbook_file(arc):
    """The T3FB002 image of a character ARC's flipbook member."""
    frames = list(tims(arc_member(arc, 1)))
    if len(frames) < 2:
        sys.exit('not a flipbook')
    # Every frame loads to one CLUT position, so the last frame's colours stay
    # (a few sets differ in one entry of their first frame).
    palette = frames[-1][0]
    data = bytearray(struct.pack('<8s4H', b'T3FB002\0', len(frames), 32, 32, 16))
    data += struct.pack('<16H', *palette)
    for _, texels in frames:
        data += texels
    return bytes(data), palette


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arc', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True, help='output file (e.g. FLIPC0.BIN)')
    a = p.parse_args()
    data, palette = flipbook_file(a.arc.read_bytes())
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(data)
    print(json.dumps(dict(frames=(len(data) - 48) // FRAME_BYTES, bytes=len(data),
                          sha256=hashlib.sha256(data).hexdigest(), palette=[hex(v) for v in palette])))


if __name__ == '__main__':
    main()
