#!/usr/bin/env python3
"""build_fighter_packs.shadow_shear against original FUN_80036180 (the shadow floor shear)."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
SHEAR_BUILD, SHEAR = 0x80036180, 0x800B0018  # MATRIX: m01 at +2, m21 at +14


def main():
    if not (RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').is_file():
        print('private original executable absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    sys.path.insert(0, str(ROOT / 'scripts'))
    try:
        from psxcpu import load_release
    except ImportError:
        print('original-code harness dependencies absent; skipped')
        return 77
    from build_fighter_packs import shadow_shear
    cpu = load_release()

    def read16(address):
        return struct.unpack('<h', cpu.read(address, 2))[0]
    for stage in range(20):
        cpu.write(SHEAR, bytes(18))
        cpu.call(SHEAR_BUILD, stage)
        m = struct.unpack('<9h', cpu.read(SHEAR, 18))
        want = (m[1], m[7])
        if m[4] or m[0] != 4096 or m[8] != 4096:
            raise AssertionError(f'stage {stage}: unexpected shear {m}')
        got = shadow_shear(read16, stage)
        if got != want:
            raise AssertionError(f'stage {stage}: original {want}, port {got}')
    print('20 original stage shadow shears matched')
    return 0


if __name__ == '__main__':
    sys.exit(main())
