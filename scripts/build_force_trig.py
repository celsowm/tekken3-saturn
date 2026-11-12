#!/usr/bin/env python3
"""Write TRIG.BIN, the executable's 0x2800-byte sine table (Japan Rev.1 0x8001E8C4) that the Tekken Force camera
reads (src/game/psx_force_camera.c), from the private USA executable. The output is retail-derived: keep it
under assets/imported or build/."""
import argparse
import struct
from pathlib import Path

from build_stage_packs import usa_file_offset

ADDRESS, BYTES = 0x8001E8C4, 0x2800


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', type=Path, required=True, help='USA SLUS_004.02')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.read_bytes()
    at = usa_file_offset(ADDRESS)
    data = exe[at:at + BYTES]
    if exe[:8] != b'PS-X EXE' or len(data) != BYTES or (
            struct.unpack_from('<h', data, 0x800)[0] != 4096 or
            struct.unpack_from('<h', data, 0x1800)[0] != -4096):
        raise ValueError('expected the original USA executable and its sine table; placeholder data is not usable')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print(f'{BYTES} bytes -> {args.out}')


if __name__ == '__main__':
    main()
