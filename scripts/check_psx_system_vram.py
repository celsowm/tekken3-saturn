#!/usr/bin/env python3
"""Check the system texture VRAM built from the USA executable against a PS1 VRAM dump.

The dump comes from a paused PCSX-Redux fight (`/api/v1/gpu/vram/raw`, 1 MiB,
1024x512 little-endian words). The effect pages (896,256) and (960,256) and the
effect CLUT rows at y=503 are loaded once from the executable's archive, so they
must match word for word. Private inputs; nothing is written."""
import argparse
import struct
import sys

from extract_psx_hud import ROOT, system_textures, word


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dump')
    p.add_argument('--exe', default=str(ROOT / 'assets/imported/tekken3-usa/SLUS_004.02'))
    a = p.parse_args()
    exe = open(a.exe, 'rb').read()
    vram = system_textures.build_vram(system_textures.members(exe, 'usa'))
    dump = open(a.dump, 'rb').read()
    if len(dump) != 1024 * 512 * 2:
        sys.exit('not a 1 MiB VRAM dump')

    def ps1(x, y):
        return struct.unpack_from('<H', dump, (y * 1024 + x) * 2)[0]
    regions = {'effect page 2 (896,256) 64x256': (896, 256, 64, 256),
               'effect page 1 (960,256) 64x256': (960, 256, 64, 256),
               'effect CLUTs (32..175,503)': (32, 503, 144, 1)}
    bad = 0
    for name, (x0, y0, w, h) in regions.items():
        diff = sum(1 for y in range(y0, y0 + h) for x in range(x0, x0 + w) if word(vram, x, y) != ps1(x, y))
        print('%-36s %d of %d words differ' % (name, diff, w * h))
        bad += diff
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
