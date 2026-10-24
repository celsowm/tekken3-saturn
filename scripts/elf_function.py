#!/usr/bin/env python3
"""Disassemble named functions of the Saturn ELF, or list only their calls.

Uses sh2eb-elf-nm and sh2eb-elf-objdump from PATH (or --tools). Each name is
the C name without the leading underscore; static functions match too.
"""
import argparse
import os
import subprocess
from pathlib import Path


def symbols(nm, elf):
    out = subprocess.run([nm, '-S', str(elf)], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in 'tTwW':
            table.setdefault(parts[3].lstrip('_'), []).append((int(parts[0], 16), int(parts[1], 16)))
    return table


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('names', nargs='+')
    p.add_argument('--elf', type=Path, default=Path('build/t30-law/tekken3_saturn.elf'))
    p.add_argument('--tools', type=Path, default=Path.home() / 'dev/saturn-tools/bin')
    p.add_argument('--calls', action='store_true', help='print only jsr/bsr lines')
    args = p.parse_args()
    tool = lambda n: str(args.tools / n) if (args.tools / n).exists() else n
    table = symbols(tool('sh2eb-elf-nm'), args.elf)
    for name in args.names:
        for start, size in table.get(name, []):
            print(f'{name} at {start:#010x}, {size} bytes')
            out = subprocess.run([tool('sh2eb-elf-objdump'), '-d', '--no-show-raw-insn',
                                  f'--start-address={start:#x}', f'--stop-address={start + size:#x}',
                                  str(args.elf)], capture_output=True, text=True, check=True).stdout
            lines = [l for l in out.splitlines() if l.startswith(' ')]
            if args.calls:
                lines = [l for l in lines if '\tjsr' in l or '\tbsr' in l or '\tjmp' in l]
            print('\n'.join(lines))
        if name not in table:
            print(f'{name}: not found')


if __name__ == '__main__':
    main()
