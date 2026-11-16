#!/usr/bin/env python3
"""Hash the code bytes of named functions in SH-2 ELFs, to prove a refactor left them unchanged.

    elf_symbol_bytes.py SYMBOL [SYMBOL...] --elf A.elf --elf B.elf

Prints each symbol's size and SHA-1 per ELF and exits 1 if any symbol differs between them. A
routine's bytes are position-independent when its branches and literal loads are PC-relative.
"""
import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / 'saturn-tools/bin'


def symbol_bytes(elf, names):
    out = subprocess.run([str(TOOLS / 'sh2eb-elf-nm'), '-S', str(elf)], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[3].lstrip('_') in names:
            found[p[3].lstrip('_')] = (int(p[0], 16), int(p[1], 16))
    result = {}
    for name, (address, size) in found.items():
        dump = subprocess.run([str(TOOLS / 'sh2eb-elf-objdump'), '-s', f'--start-address={address:#x}',
                               f'--stop-address={address + size:#x}', str(elf)], capture_output=True, text=True, check=True).stdout
        # objdump -s rows: " address  8hex 8hex 8hex 8hex  ascii"; the hex columns end at 36.
        data = ''.join(''.join(row[1:].split(' ', 1)[1][:35].split()) for row in dump.splitlines() if row.startswith(' '))
        result[name] = (size, hashlib.sha1(data.encode()).hexdigest()[:16])
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('symbol', nargs='+')
    p.add_argument('--elf', type=Path, action='append', required=True)
    a = p.parse_args()
    tables = [symbol_bytes(e, set(a.symbol)) for e in a.elf]
    differs = False
    for name in a.symbol:
        row = [t.get(name) for t in tables]
        same = all(r == row[0] and r for r in row)
        differs |= not same
        print(name, *row, 'same' if same else 'DIFFERENT')
    sys.exit(1 if differs else 0)


if __name__ == '__main__':
    main()
