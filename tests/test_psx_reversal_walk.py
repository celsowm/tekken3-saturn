#!/usr/bin/env python3
"""Run authored reversal lists through actual BranchFindReversal and C."""
import argparse
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'


def row(command=16, condition=0x45, target=100, restriction=0, parameter=0, first=2, last=7):
    return struct.pack('<HBBHHBBBB', command, restriction, condition, parameter, target, 33, first, last, 1)


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
    own, shared, self_f, opp_f, move, descriptor = 0x801E0000, 0x80017C78, 0x800A96F0, 0x800AAF7C, 0x801E1000, 0x801E1100
    cpu.write(self_f + 0x16, struct.pack('<hh', 0, 0))
    cpu.write(opp_f + 0x16, struct.pack('<hh', 1, 1))
    cpu.write(self_f + 0x402, struct.pack('<HHH', 0x200, 0, 0))
    cpu.write(opp_f + 0x54, struct.pack('<I', move))
    cpu.write(move + 0x24, struct.pack('<II', 0, descriptor))
    cpu.write(descriptor, bytes([6, 10, 14, 17]))
    end, ret = row(0xC000), row(0xC00D)
    layouts = [
        ([row(), end], [ret]),
        ([row(condition=0), row(target=101), end], [ret]),
        ([row(restriction=2), row(restriction=1, target=102), end], [ret]),
        ([row(condition=0x4A, parameter=295), row(target=103), end], [ret]),
        ([row(0xC00C, target=0), row(target=104), end], [row(target=105), ret]),
        ([row(0xC00C, target=0), row(target=106), end],
         [row(condition=0x4A, parameter=295, target=107), ret]),
        ([row(condition=0x49, parameter=10, target=108), row(target=109), end], [ret]),
        ([row(condition=0x4C), row(command=1, target=110), row(target=111), end], [ret]),
    ]
    total = 0
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        for index, (own_rows, common_rows) in enumerate(layouts):
            own_data, common_data = b''.join(own_rows), b''.join(common_rows)
            cpu.write(own, own_data)
            cpu.write(shared, common_data)
            (folder / 'own.bin').write_bytes(own_data)
            (folder / 'shared.bin').write_bytes(common_data)
            lines, expected = [], []
            for frame, hit, angle, other_angle, slot, attack in itertools.product(
                    (0, 1, 2, 7, 8), (0, 1), (0, 0x2AAA), (0, 0x2AA9), (295, 355), (0x412, 0x10F)):
                cpu.write(self_f + 0xCE, bytes([hit]))
                cpu.write(self_f + 0x30, struct.pack('<h', angle))
                cpu.write(opp_f + 0x30, struct.pack('<h', other_angle))
                cpu.write(opp_f + 0xA0, struct.pack('<h', slot))
                cpu.write(opp_f + 0x64, struct.pack('<h', attack))
                result = cpu.call(0x8002E95C, own, frame, self_f, opp_f) & 0xFFFFFFFF
                command = struct.unpack('<H', cpu.read(result, 2))[0]
                expected.append([0, -1] if command == 0xC000 else
                                [1, struct.unpack('<H', cpu.read(result + 6, 2))[0]])
                lines.append(f'{frame} {hit} {angle} {other_angle} {slot} {attack}')
            output = subprocess.run([str(args.runner.resolve()), str(folder / 'own.bin'), str(folder / 'shared.bin')],
                                    input='\n'.join(lines) + '\n', capture_output=True, text=True, check=True)
            actual = [list(map(int, line.split())) for line in output.stdout.splitlines()]
            if len(actual) != len(expected):
                raise AssertionError('wrong result count')
            for line, want, got in zip(lines, expected, actual):
                if want != got:
                    raise AssertionError(f'layout {index}, {line}: original {want}, native {got}')
            total += len(lines)
    print(f'{total} original BranchFindReversal cases matched, including common CALL/RETURN')
    return 0


if __name__ == '__main__':
    sys.exit(main())
