#!/usr/bin/env python3
"""Particle matrix/quads/depth/culling vs unpatched original EffectsUpdate.

Runs the real original billboard draw path without stepping object lifetime;
hooks are read-only. No float/Godot projection is used as a golden result.
"""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT/'third_party/Tekken3Remake/tools/research'
MODEL, OBJECT, HEADER, OT = 0x80180000, 0x80180100, 0x80181000, 0x80190000
SENTINEL = 0x800A0D30


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    a = parser.parse_args()
    exe = RESEARCH.parent.parent/'work/jp_rev1/exe.bin'
    if not exe.is_file():
        return 77
    sys.path.insert(0, str(RESEARCH))
    try:
        from psxcpu import load_release
        from unicorn import UC_HOOK_CODE
        from unicorn.mips_const import UC_MIPS_REG_A0
    except ImportError:
        return 77
    cpu = load_release()
    observed = {}

    def matrix_hook(uc, address, size, data):
        ptr = uc.reg_read(UC_MIPS_REG_A0)
        observed['matrix'] = list(struct.unpack('<9h', cpu.read(ptr, 18)))

    def quad_hook(uc, address, size, data):
        ptr = uc.reg_read(UC_MIPS_REG_A0)
        observed['xy'] = list(struct.unpack('<8h', cpu.read(0x1F800000, 16)))
        observed['depth'] = list(struct.unpack('<4H', cpu.read(ptr, 8)))

    cpu.uc.hook_add(UC_HOOK_CODE, matrix_hook, begin=0x800828CC, end=0x800828CC)
    cpu.uc.hook_add(UC_HOOK_CODE, quad_hook, begin=0x8006E734, end=0x8006E734)
    w = lambda address, value: cpu.write(address, struct.pack('<I', value & 0xFFFFFFFF))
    cpu.write(0x800A0D38, b'\1\1')  # No automatic eight-object setup.
    w(0x800AE16C, 0)  # No global sprite suppression.
    w(0x800A911C, HEADER)
    w(HEADER+4, OT)
    w(SENTINEL+4, OBJECT)
    w(0x800AE3C4, 0)

    raw_exe = exe.read_bytes()
    models = [list(struct.unpack_from('<16h', raw_exe, addr-0x80010000+0x800))
              for addr in (0x8002554C, 0x8002593C, 0x8002556C, 0x800256E0, 0x80025700)]
    rng = random.Random(0x6F8A8)
    cases = []
    # Both wrapped delta boundaries, negative/near-plane depths and OT bias.
    for depth in (-32768, -1, 0, 1, 159, 160, 32767, 32768, 65535, 65536):
        for h in (0, 1, 500, 65535):
            for flags in (0x40, 0x60):
                cases.append([0, 0, 0, 0, 0, 0, h, 0, -100, depth, flags, *models[0]])
    for _ in range(1200):
        camera = [rng.randrange(-32768, 32768) for _ in range(3)]
        camera += [rng.choice([-2147483648, 2147483647, rng.randrange(-100000, 100001)]) for _ in range(3)]
        camera += [rng.choice([0, 1, 500, 65535, rng.randrange(65536)])]
        position = [rng.choice([-2147483648, 2147483647, rng.randrange(-100000, 100001)]) for _ in range(3)]
        model = (rng.choice(models) if rng.randrange(3) else
                 [rng.randrange(-32768, 32768) if k % 4 != 3 else 0 for k in range(16)])
        cases.append([*camera, *position, rng.choice([0x40, 0x60, 0xC0, 0xE0]), *model])
    # Real varied-input fight coordinates: not just synthetic matrix cases.
    captured = ROOT/'build/parity-particle-projection-original.json'
    captured_rows = []
    if captured.is_file():
        for row in json.loads(captured.read_text())['projections']:
            view = row['camera'];ctrl = row['gte_control']
            ptr = int(row['model'], 16)
            model = list(struct.unpack_from('<16h', raw_exe, ptr-0x80010000+0x800))
            cases.append([*view[:3], *view[5:8], ctrl[26] & 65535, *row['position'],
                          (row['flags_word'] >> 8) & 255, *model])
            captured_rows.append(row)
    expected = []
    visible = 0
    for case in cases:
        pitch, yaw, roll, x, y, z, h, px, py, pz, flags, *model = case
        cpu.write(MODEL, struct.pack('<16h', *model))
        obj = bytearray(0xA0)
        struct.pack_into('<3I', obj, 0, SENTINEL, SENTINEL, MODEL)
        obj[12:15] = bytes([1, flags, 1])
        struct.pack_into('<3i', obj, 0x28, px, py, pz)
        cpu.write(OBJECT, obj)
        cpu.write(OT, b'\xff\xff\xff\0'*1024)
        cpu.write(0x800A8A88, struct.pack('<3i', pitch, yaw, roll))
        cpu.write(0x800A8A9C, struct.pack('<3i', x, y, z))
        cpu.gte.write_ctrl(24, 192 << 16)
        cpu.gte.write_ctrl(25, 240 << 16)
        cpu.gte.write_ctrl(26, h)
        observed.clear()
        cpu.call(0x8006F8A8, 0)
        if set(observed) != {'matrix', 'xy', 'depth'}:
            raise AssertionError(f'original draw path was not executed: {observed}')
        bucket = ((observed['depth'][1] >> 5) & 1023) - (5 if flags & 0x20 else 0)
        # The actual OT link, rather than a reimplementation of culling.
        linked = bucket >= 0 and struct.unpack('<I', cpu.read(OT+4*bucket, 4))[0] == ((OBJECT+0x50) & 0xFFFFFF)
        if linked:
            xy = [v for off in (0x58, 0x60, 0x68, 0x70) for v in struct.unpack('<2h', cpu.read(OBJECT+off, 4))]
            if xy != observed['xy']:
                raise AssertionError('linked primitive differs from original GTE coordinates')
        visible += linked
        expected.append(observed['matrix'] + observed['xy'] + observed['depth'] + [bucket, int(linked)])
    result = subprocess.run([str(a.runner.resolve()), str(exe)], input=''.join(
        ' '.join(map(str, row))+'\n' for row in cases), text=True, capture_output=True, check=True)
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError('C projection case count mismatch')
    for i, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            raise AssertionError(dict(case=i, input=cases[i], expected=want, actual=got))
    for i, row in enumerate(captured_rows, len(cases)-len(captured_rows)):
        xy = [v for point in row['xy'] for v in point]
        if actual[i][9:17] != xy:
            raise AssertionError(f'fight capture mismatch: {i}')
    print(json.dumps(dict(matched_cases=len(cases), fight_capture_quads=len(captured_rows),
                          original_ot_links=visible, fields_per_case=23,
                          scope='Original matrices, four projected vertices/depths and actual OT insertion; no native drawing/blend proof')))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
