#!/usr/bin/env python3
"""Print raw words of DIVMOT move records by slot (private local banks).

  move_record_words.py BANK SLOT [SLOT ...] [--common COMMON]

Shows the move index entry, the 56-byte row's damage word (+20), descriptor (+40), active window (+45, +46), reaction
(+50) and close reaction (+52). Common-bank entries (0x40000000) are read from
--common.
"""
import argparse
import struct
from pathlib import Path


def sections(data):
    return [struct.unpack_from('<I', data, 4 + 4 * i)[0] for i in range(15)]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('bank', type=Path)
    p.add_argument('slots', type=int, nargs='+')
    p.add_argument('--common', type=Path)
    a = p.parse_args()
    bank = a.bank.read_bytes()
    common = a.common.read_bytes() if a.common else None
    for slot in a.slots:
        entry = struct.unpack_from('<I', bank, sections(bank)[2] + 4 * slot)[0]
        data, index, where = bank, entry, 'own'
        if entry == 0x3FFFFFFF:
            print(f'slot {slot}: empty')
            continue
        if entry >= 0x40000000:
            if common is None:
                print(f'slot {slot}: common {entry - 0x40000000} (pass --common)')
                continue
            data, index, where = common, entry - 0x40000000, 'common'
        row = sections(data)[1] + 56 * index
        damage, = struct.unpack_from('<H', data, row + 20)
        reaction, close = struct.unpack_from('<HH', data, row + 50)
        descriptor, = struct.unpack_from('<I', data, row + 40)
        first, last = data[row + 45], data[row + 46]
        print(f'slot {slot}: {where} {index} damage {damage:#x} reaction {reaction:#x} close {close}'
              f' descriptor {descriptor:#x} active {first}-{last}')


if __name__ == '__main__':
    main()
