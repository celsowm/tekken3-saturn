#!/usr/bin/env python3
"""Write TRIG.BIN, the executable's 0x2800-byte sine table (Japan Rev.1 0x8001E8C4) that the Tekken Force camera
reads (src/game/psx_force_camera.c), from the private USA executable. The output is retail-derived: keep it
under assets/imported or build/."""
import argparse
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
    args.out.write_bytes(exe[at:at + BYTES])
    print(f'{BYTES} bytes -> {args.out}')


if __name__ == '__main__':
    main()
