#!/usr/bin/env python3
"""Compare sound ring writes/clears/playback with unpatched original MIPS.

Runs the actual three sound-ring functions; audio entry points alone are
stubbed to observe logical dispatch. This does not compare audible samples.
"""
import argparse
import json
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
        print('original MIPS dependencies absent; skipped')
        return 77
    cpu = load_release()
    base = 0x80180000
    ring = base + 0x7A48
    cpu.write(0x800AE330, struct.pack('<I', base))
    cpu.write(0x8009C054, bytes(4))  # Normal VS bank 0.
    events = []
    cpu.stub(0x80040E58, lambda c, code, *unused: events.append([1, 0, code & 65535]) or 0)
    cpu.stub(0x80040EB8, lambda c, player, voice, code, *unused:
             events.append([2, player, code & 65535]) or 0)
    cpu.stub(0x80041038, lambda c, player, code, *unused:
             events.append([3, player, code & 65535]) or 0)
    commands = []
    def add(op, *values):
        commands.append([op, *values, *([0] * (5 - len(values)))])
    # Empty, stale and all known/unknown kind bytes, every playback half-frame.
    for kind in (0, 1, 2, 3, 4, 255):
        add(0, 0, (1 << 24) | (kind << 16) | 0xFABC)
        for play in range(600):
            add(3, play)
    # Every record cursor and delay around wrap boundaries; compare the full
    # ring, including code/player bytes which a clear must leave untouched.
    for delay in (0, 1, 2, 299, 300, 301, 599):
        add(0, delay, 0xA5A5A5A5)
        for record in range(300):
            add(1, 7, record, record % 5, record & 1, 0xF000 + record)
            add(2, (record + 1) % 300)
    # Record gate ignores every mode except 7, with byte/word truncation.
    for mode in range(9):
        add(0, 0, 0xA5A5A5A5)
        add(1, mode, 299, 0x103, 0x101, 0x18000)
        add(3, 598)
    # Continuous multi-frame bursts, backlog draining and playback over wraps.
    rng = random.Random(0x33780)
    add(0, 0, 0)
    for frame in range(1200):
        record = (frame + 1) % 300
        add(2, record)
        for _ in range(rng.randrange(5) if frame < 600 else 0):
            add(1, 7, record, rng.randrange(1, 4), rng.randrange(2), rng.getrandbits(16))
        add(3, (2 * frame) % 600)
        add(3, (2 * frame + 1) % 600)
    expected = []
    for op, a, b, c, d, e in commands:
        events.clear()
        ok = 1
        if op == 0:
            cpu.write(0x8009C06C, struct.pack('<I', a))
            cpu.write(ring, struct.pack('<I', b) * 300)
        elif op == 1:
            cpu.write(0x8009C050, struct.pack('<I', a))
            cpu.write(0x8009C060, struct.pack('<I', b))
            cpu.call(0x80033780, c, d, e)
        elif op == 2:
            cpu.write(0x8009C060, struct.pack('<I', a))
            cpu.call(0x80033838)
        elif op == 3:
            cpu.write(0x8009C064, struct.pack('<I', a))
            cpu.call(0x800338E0)
            ok = int(bool(events))
        assert len(events) <= 1
        expected.append(dict(ok=ok, delay=struct.unpack('<i', cpu.read(0x8009C06C, 4))[0],
                             event=events[0] if events else [0, 0, 0],
                             ring=list(struct.unpack('<300I', cpu.read(ring, 1200)))))
    result = subprocess.run([str(args.runner.resolve())], input=''.join(
        ' '.join(map(str, cmd)) + '\n' for cmd in commands), text=True, capture_output=True, check=True)
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(actual) == len(expected), (len(actual), len(expected))
    for n, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            changed = [i for i, (x, y) in enumerate(zip(want['ring'], got['ring'])) if x != y]
            raise AssertionError(dict(operation=n, command=commands[n], ring_differences=changed,
                                      expected={k:v for k,v in want.items() if k != 'ring'},
                                      actual={k:v for k,v in got.items() if k != 'ring'}))
    print(json.dumps(dict(matched_operations=len(commands), ring_words_checked_per_operation=300,
                          original_functions=['80033780', '80033838', '800338E0'],
                          audio_entry_points_stubbed=True, bank=0)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
