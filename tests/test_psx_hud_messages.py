#!/usr/bin/env python3
"""Compare normal-VS text selection with original JP Rev.1 routines.

Only PrintText is intercepted, recording its format and variadic arguments.
Message selection, character metadata lookup and name length run in original
MIPS code. Original images and executable remain private. Replay blink tests
separately exercise the documented ReplayUpdate branch's counter boundaries.
"""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    args = parser.parse_args()
    if not (RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').is_file():
        print('private original executable absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    try:
        from psxcpu import load_release
        from unicorn.mips_const import UC_MIPS_REG_SP
    except ImportError:
        print('original-code harness dependencies absent; skipped')
        return 77
    cpu = load_release()
    calls = []

    def cstring(address):
        out = bytearray()
        for i in range(128):
            b = cpu.read(address + i, 1)[0]
            if not b:
                return out.decode('ascii')
            out.append(b)
        raise ValueError('unterminated original HUD text')

    def print_text(c, fmt, colour, font, x):
        sp = c.uc.reg_read(UC_MIPS_REG_SP)
        y, extra = struct.unpack('<II', c.read(sp + 16, 8))
        text = cstring(fmt)
        if not text.startswith('%c%f%H%V'):
            raise ValueError('unexpected original format: ' + text)
        text = text[8:]
        if '%1x' in text:
            text = text.replace('%1x', format(extra & 15, 'x'))
        if '%s' in text:
            text = text.replace('%s', cstring(extra))
        calls.append([text, colour, font, (x + 0x8000) % 65536 - 0x8000, y])
        return 0

    cpu.stub(0x8004D15C, print_text)

    def write(address, fmt, value):
        cpu.write(address, struct.pack('<' + fmt, value))

    rows, expected = [], []
    coverage = {'intro': 0, 'result': 0, 'winner': 0, 'replay_contract': 0}

    def case(state, frame=0, played=1, maximum=3, flags=0, request=0,
             humans=3, winner=0, name='PAUL', key=0):
        rows.append(f'{state} {frame} {played} {maximum} {flags} {request} {humans} {winner} 7 0 {name}')
        calls.clear()
        write(0x800AFF50, 'I', 1)  # Normal VS, not the Arcade result labels.
        write(0x80095884, 'i', frame)
        write(0x800958A8, 'i', played)
        write(0x800AE404, 'H', maximum)
        write(0x800AE340, 'I', flags)
        write(0x80097358, 'I', request)
        write(0x800AE3D8, 'H', humans)
        for i, base in enumerate((0x800A96F0, 0x800AAF7C)):
            write(base + 0x46, 'h', 0 if winner < 0 else 1 if winner == i else -1)
            write(base + 0x14, 'h', key)
        if state == 0 and frame:
            cpu.call(0x8003DF60)
            coverage['intro'] += 1
        elif state == 2:
            cpu.call(0x8003E200)
            coverage['result'] += 1
        elif state in (6, 9):
            cpu.call(0x8003E408)
            coverage['winner'] += 1
        expected.append(list(calls))

    for played in range(1, 10):
        for maximum in (1, 3, 5, 9):
            for frame in (0, 1, 45, 46, 60, 61, 99, 100, 105, 106, 149, 150, 151):
                case(0, frame, played, maximum)
    for flags in range(64):
        for request in (0, 1):
            case(2, flags=flags, request=request)
    for state in (6, 9):
        for humans in range(4):
            for winner in (-1, 0, 1):
                for name, key in (('PAUL', 0), ('LAW', 4)):
                    case(state, humans=humans, winner=winner, name=name, key=key)
    for state in (1, 3, 4, 5):
        case(state)
    # Ending playback returns to RoundFlow and draws the winner immediately.
    case(6)
    rows[-1] = rows[-1].replace(' 7 0 PAUL', ' 4 0 PAUL')
    # Source branch: only replay mode 1 draws, on frame-counter bit 4.
    for mode in range(1, 6):
        for counter in (0, 15, 16, 17, 31, 32, 47, 48, 0xFFFFFFFF):
            rows.append(f'4 0 1 3 0 0 3 0 {mode} {counter} PAUL')
            expected.append([['REPLAY', 7, 1, 244, 30]] if mode == 1 and counter & 16 else [])
            coverage['replay_contract'] += 1
    result = subprocess.run([str(args.runner.resolve())], input='\n'.join(rows) + '\n',
                            text=True, capture_output=True, check=True)
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError(f'wrong output count: {len(actual)} != {len(expected)}')
    for row, want, got in zip(rows, expected, actual):
        if want != got:
            raise AssertionError(f'{row}: original {want}, native {got}')
    print(json.dumps({'matched': len(rows), 'coverage': coverage}, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
