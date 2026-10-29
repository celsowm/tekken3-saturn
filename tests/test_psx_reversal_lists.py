#!/usr/bin/env python3
"""Check the USA-normalized lists against actual original JP membership code."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    parser.add_argument('--lists', type=Path, default=ROOT / 'assets/imported/tekken3-usa/pose-tables/psx_reversal_lists.bin')
    args = parser.parse_args()
    if not args.lists.is_file() or not (RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').is_file():
        print('private original executable/lists absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    try:
        from psxcpu import load_release
    except ImportError:
        print('original-code harness dependencies absent; skipped')
        return 77
    data = args.lists.read_bytes()
    if len(data) < 108 or struct.unpack_from('<H', data)[0] != 52:
        raise AssertionError('wrong private list format')
    tags = {-32768, -1, 0, 1, 32767}
    offsets = struct.unpack_from('<53H', data, 2)
    if offsets[0] != 108 or offsets[-1] != len(data):
        raise AssertionError('list offsets do not cover the exact blob')
    for start, end in zip(offsets, offsets[1:]):
        if start >= end or end > len(data) or start % 2 or end % 2:
            raise AssertionError('invalid list boundary')
        words = struct.unpack_from('<' + 'h' * ((end - start) // 2), data, start)
        if words[-1] != 0 or any(word <= 0 for word in words[:-1]):
            raise AssertionError('invalid positive list/terminator')
        for word in words[:-1]:
            tags.update((word - 1, word, word + 1))
    cpu = load_release()
    rows, expected = [], []
    for index in range(52):
        for tag in sorted(tags):
            cpu.write(0x801E0000, struct.pack('<h', tag))
            expected.append(cpu.call(0x8002D4B8, 0x801E0002, index) & 0xFFFFFFFF)
            rows.append(f'{index} {tag}')
    result = subprocess.run([str(args.runner.resolve()), str(args.lists.resolve())],
                            input='\n'.join(rows) + '\n', text=True, capture_output=True, check=True)
    actual = [int(line) for line in result.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError('wrong result count')
    for row, want, got in zip(rows, expected, actual):
        if want != got:
            raise AssertionError(f'{row}: original {want}, native {got}')
    print(f'{len(rows)} original membership cases matched across all 52 USA-normalized lists')
    return 0


if __name__ == '__main__':
    sys.exit(main())
