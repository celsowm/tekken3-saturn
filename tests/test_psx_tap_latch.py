#!/usr/bin/env python3
"""Compare the tap latch, including bug #52, against original MIPS code."""
import argparse
import itertools
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    args = parser.parse_args()
    if not (RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').is_file():
        print('private original executable absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    try:
        from psxcpu import load_release
        from unicorn.mips_const import UC_MIPS_REG_A2
    except ImportError:
        print('original-code harness dependencies absent; skipped')
        return 77
    cpu = load_release()
    fighter = 0x800A96F0
    rows, expected = [], []
    # Every combination of valid buttons, retained flags and condition marker,
    # with both same-frame and advancing-frame calls.
    cases = []
    for previous, entry, flags, used, same in itertools.product(
            (5, 0x15, 0x25, 0x35), (5, 0x15, 0x25, 0x35, 0xF5),
            range(8), (0, 1, 2), (False, True)):
        cases.append((previous, flags & 1, (flags >> 1) & 1,
                      (flags >> 2) & 1, used, entry, 7, 7 if same else 6, 0xABCD1234))
    rng = random.Random(0x40D68)
    for _ in range(3000):
        cases.append((rng.randrange(65536), rng.randrange(3), rng.randrange(3),
                      rng.randrange(3), rng.randrange(3), rng.randrange(256),
                      rng.randrange(2**32), rng.randrange(2**32), rng.randrange(2**32)))
    cpu.write(fighter + 0x1E, b'\0')
    cpu.write(fighter + 0x40C, struct.pack('<I', 0))
    for row in cases:
        previous, lp, rp, both, used, entry, frame, seen, scratch = row
        cpu.write(fighter + 0x8C, bytes([used]))
        cpu.write(fighter + 0xE0, bytes([lp, rp, both, 0]))
        cpu.write(fighter + 0x41C, bytes([entry]))
        cpu.write(0x8009746C, struct.pack('<H', previous))
        cpu.write(0x80095884, struct.pack('<II', frame, seen))
        cpu.call(0x80040D68, fighter, 0, scratch)
        prev = struct.unpack('<H', cpu.read(0x8009746C, 2))[0]
        taps = list(cpu.read(fighter + 0xE0, 3))
        marker = cpu.read(fighter + 0x8C, 1)[0]
        expected.append([prev, *taps, marker, cpu.uc.reg_read(UC_MIPS_REG_A2)])
        rows.append(' '.join(map(str, row)))
    result = subprocess.run([str(args.runner.resolve())], input='\n'.join(rows) + '\n',
                            text=True, capture_output=True, check=True)
    actual = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError(f'wrong output count {len(actual)} != {len(expected)}')
    for row, want, got in zip(rows, expected, actual):
        if want != got:
            raise AssertionError(f'{row}: original {want}, native {got}')
    print(f'{len(rows)} original MIPS tap-latch cases matched')
    return 0


if __name__ == '__main__':
    sys.exit(main())
