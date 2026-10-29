#!/usr/bin/env python3
"""Differential predicate check against original ReversalCondition MIPS code.

Only the animation-list query is intercepted; joint matching executes in the
original routine. List query invocation and signed parameter are also checked.
This is not the reversal walker, hit replacement or integrated fight test.
"""
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
    except ImportError:
        print('original-code harness dependencies absent; skipped')
        return 77
    cpu = load_release()
    row, self_f, opp_f, move, descriptor = (0x801E0000 + i * 0x100 for i in range(5))
    query_calls, query_result = [], [0]

    def query(c, animation, index, *rest):
        query_calls.append(index if index < 2**31 else index - 2**32)
        return query_result[0]

    cpu.stub(0x8002D4B8, query)
    cpu.write(opp_f + 0x54, struct.pack('<I', move))
    cpu.write(move, struct.pack('<I', 0x801E1000))
    cpu.write(move + 0x28, struct.pack('<I', descriptor))
    cases = []
    angles = (-32768, -1, 0, 0x2AA9, 0x2AAA, 32767)
    for kind, a, b, p in itertools.product(range(0x45, 0x4C), angles, angles,
                                          (0, 6, 17, 255, 0x7FFF, 0x8000, 0xFFFF)):
        for attack, flags, hit in ((0x412, 0, 1), (0x217, 0, 0),
                                   (0x10F, 0, 1), (0x412, 0x800, 1)):
            cases.append((kind, p, a, b, hit, flags, attack, p if p < 32768 else p - 65536,
                          6, 10, 14, 17, 1))
    rng = random.Random(0x2EB24)
    for _ in range(2000):
        cases.append((rng.randrange(0x44, 0x4D), rng.randrange(65536),
                      rng.randrange(-32768, 32768), rng.randrange(-32768, 32768),
                      rng.randrange(3), rng.getrandbits(32),
                      rng.choice((0x412, 0x217, 0x10F, 0x800, -1)),
                      rng.randrange(-32768, 32768), *(rng.randrange(256) for _ in range(4)),
                      rng.randrange(2)))
    rows, expected = [], []
    for case in cases:
        kind, p, a, b, hit, flags, attack, slot, *tail = case
        joints, animation_result = tail[:4], tail[4]
        cpu.write(row + 3, struct.pack('<BH', kind, p))
        cpu.write(self_f + 0x30, struct.pack('<h', a))
        cpu.write(opp_f + 0x30, struct.pack('<h', b))
        cpu.write(self_f + 0xCE, bytes([hit]))
        cpu.write(move + 0x24, struct.pack('<I', flags))
        cpu.write(opp_f + 0x64, struct.pack('<h', attack))
        cpu.write(opp_f + 0xA0, struct.pack('<h', slot))
        cpu.write(descriptor, bytes(joints))
        query_calls.clear()
        query_result[0] = animation_result
        result = cpu.call(0x8002EB24, row, self_f, opp_f) & 0xFFFFFFFF
        expected.append([int(bool(result)), int(bool(query_calls)), query_calls[0] if query_calls else 0])
        rows.append(' '.join(map(str, case)))
    output = subprocess.run([str(args.runner.resolve())], input='\n'.join(rows) + '\n',
                            capture_output=True, text=True, check=True)
    actual = [list(map(int, line.split())) for line in output.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError(f'wrong result count {len(actual)} != {len(expected)}')
    for row, want, got in zip(rows, expected, actual):
        if got != want:
            raise AssertionError(f'{row}: original {want}, native {got}')
    print(f'{len(rows)} original MIPS reversal-predicate cases matched')
    return 0


if __name__ == '__main__':
    sys.exit(main())
