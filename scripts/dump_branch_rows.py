#!/usr/bin/env python3
"""Print a move slot's record and branch rows from a private DIVMOT bank.

Slots that point into the common bank (id >= 0x40000000) are read from the
--common bank (type 99). The row walk follows 0xC00C calls into the EXE
common branch table (--common-branches) and stops at the 0xC000 end row.
"""
import argparse
import struct
from pathlib import Path

END, CALL, RETURN = 0xC000, 0xC00C, 0xC00D


IMPORTED = Path('assets/imported/tekken3-usa')


class Rows:
    def __init__(self, data):
        self.data = data

    def row(self, index):
        command, restriction, condition, parameter, target, flags, first, last, entry = struct.unpack_from(
            '<HBBhhBBBB', self.data, index * 12)
        return dict(command=command, restriction=restriction, condition=condition, parameter=parameter,
                    target=target, flags=flags, first=first, last=last, entry=entry)


class Bank:
    def __init__(self, path):
        self.data = path.read_bytes()
        self.type = self.data[1]
        self.sections = struct.unpack_from('<15I', self.data, 4)
        self.rows = Rows(self.data[self.sections[0]:self.sections[1]])

    def move(self, index):
        p = self.sections[1] + index * 56
        d = self.data
        return dict(index=index, state=hex(struct.unpack_from('<I', d, p + 4)[0]),
                    attack=hex(struct.unpack_from('<H', d, p + 8)[0]),
                    branch=struct.unpack_from('<I', d, p + 12)[0], length=d[p + 24],
                    air=(d[p + 25], d[p + 26]), hold=d[p + 27],
                    flags=hex(struct.unpack_from('<I', d, p + 36)[0]),
                    active=(d[p + 45], d[p + 46]))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('bank', type=Path)
    p.add_argument('slot', type=int)
    p.add_argument('--common', type=Path, default=IMPORTED / 'divmot/279/bank_99.t3logic')
    p.add_argument('--common-branches', type=Path, default=IMPORTED / 'pose-tables/psx_common_branches.bin')
    args = p.parse_args()
    bank, common = Bank(args.bank), Bank(args.common)
    shared = Rows(args.common_branches.read_bytes())
    if common.type != 99:
        raise SystemExit(f'{args.common} is not the common bank (type {common.type})')
    slot_id = struct.unpack_from('<I', bank.data, bank.sections[2] + args.slot * 4)[0]
    owner, index = (common, slot_id - 0x40000000) if slot_id >= 0x40000000 else (bank, slot_id)
    move = owner.move(index)
    print('slot', args.slot, 'common' if owner is common else 'own', move)
    i, table, back = move['branch'], owner.rows, None
    for _ in range(4096):
        r = table.row(i)
        tag = 'common ' if table is shared else ''
        print(f'  {tag}row {i}: cmd={r["command"]:#06x} restr={r["restriction"]} cond={r["condition"]} '
              f'param={r["parameter"]} -> slot {r["target"]} flags={r["flags"]:#04x} '
              f'frames {r["first"]}-{r["last"]} entry {r["entry"]}')
        if r['command'] == END:
            break
        if r['command'] == CALL:
            back, table, i = (table, i), shared, r['target']
            continue
        if r['command'] == RETURN and back:
            (table, i), back = back, None
        i += 1


if __name__ == '__main__':
    main()
