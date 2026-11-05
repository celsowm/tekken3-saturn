#!/usr/bin/env python3
"""Hand channels against original HandFaceCommand (0x800346F8) and FighterHandPoses (0x80034BC4)."""
import argparse
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
HAND_FACE, HAND_POSES, SELECT_VARIANT = 0x800346F8, 0x80034BC4, 0x80034A98
FIGHTER, FIGHTER_BYTES = 0x801A0000, 0x1AE4
CHAR_ID, CURRENT = 0x18, 0x127C   # s16; the target and rate follow 4 and 8 bytes on
REPLAY_MODE, SKIP_FLAGS = 0x8009C050, (0x800958B8, 0x8009588C)
VALUES = (0, 1, 127, 128, 255, 256, 383, 384, 511, 0x200, 0x201, 0x203, 0x206, 0x207, -1, 0x7FFF, -0x8000)


def variant_of(value):
    if value > 0x200:
        return (value - 0x1FD) & 0xFF
    v = value >> 7
    return (v - 1 if v else 0) & 0xFF


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
    # The vertex block swap and Gon's palette are the pack's (fighter_hands.c), not the channels.
    cpu.stub(SELECT_VARIANT, lambda c, *a: 0)
    cpu.write(FIGHTER, bytes(FIGHTER_BYTES))
    cpu.write(REPLAY_MODE, bytes(4))
    for flag in SKIP_FLAGS:
        cpu.write(flag, bytes(4))
    rng = random.Random(0x346F8)

    def value():
        return rng.choice(VALUES) if rng.random() < 0.5 else rng.randrange(-0x300, 0x400)
    cases = []
    for _ in range(4000):
        state = [value(), value(), rng.choice((0, 1, 10, 51, 64, 256, 512, -3)) if rng.random() < 0.3
                 else rng.randrange(0, 200)] + [value(), value(), rng.randrange(0, 200)]
        if rng.random() < 0.5:
            cases.append(state + [1, 0, 0, 0])
        else:
            cases.append(state + [0, rng.randrange(4), rng.randrange(12),
                                  rng.choice((0, 1, -1, -2, 10, 127, -128)) if rng.random() < 0.4
                                  else rng.randrange(-128, 128)])
    expected = []
    for row in cases:
        c0, t0, r0, c1, t1, r1, op, hands, shape, speed = row
        cpu.write(FIGHTER + CHAR_ID, struct.pack('<h', 0))
        cpu.write(FIGHTER + CURRENT, struct.pack('<6h', c0, c1, t0, t1, r0, r1))
        if op:
            cpu.call(HAND_POSES, FIGHTER)
        else:
            cpu.call(HAND_FACE, FIGHTER, hands, shape, speed & 0xFFFFFFFF)
        c0, c1, t0, t1, r0, r1 = struct.unpack('<6h', cpu.read(FIGHTER + CURRENT, 12))
        expected.append((c0, t0, r0, c1, t1, r1, variant_of(c0) if op else 0, variant_of(c1) if op else 0))
    result = subprocess.run([str(args.runner.resolve())], text=True, capture_output=True,
                            input=''.join(' '.join(map(str, row)) + '\n' for row in cases), check=True)
    actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError('wrong native output count')
    for case, want, got in zip(cases, expected, actual):
        if not case[6]:
            got = got[:6] + (0, 0)  # a command leaves the variants to the next frame
        if want != got:
            raise AssertionError(f'{case}: original {want}, native {got}')
    print(f'{len(cases)} original MIPS hand channel cases matched')
    return 0


if __name__ == '__main__':
    sys.exit(main())
