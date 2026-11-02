#!/usr/bin/env python3
"""Compare two `probe --vdp2-trace` logs: the VDP2 registers each program holds at one field.

    scripts/vdp2_trace_diff.py a/regs.csv b/regs.csv --field 800
    scripts/vdp2_trace_diff.py a/regs.csv --changes-from 300 --changes-to 500
"""
import argparse
import csv
from pathlib import Path

NAMES = {
    0x000: 'TVMD', 0x002: 'EXTEN', 0x006: 'VRSIZE', 0x00E: 'RAMCTL',
    0x010: 'CYCA0L', 0x012: 'CYCA0U', 0x014: 'CYCA1L', 0x016: 'CYCA1U',
    0x018: 'CYCB0L', 0x01A: 'CYCB0U', 0x01C: 'CYCB1L', 0x01E: 'CYCB1U',
    0x020: 'BGON', 0x022: 'MZCTL', 0x024: 'SFSEL', 0x026: 'SFCODE', 0x028: 'CHCTLA', 0x02A: 'CHCTLB',
    0x02C: 'BMPNA', 0x02E: 'BMPNB', 0x030: 'PNCNA', 0x032: 'PNCNB', 0x034: 'PNCNC', 0x036: 'PNCND',
    0x038: 'PNCR', 0x03A: 'PLSZ', 0x03C: 'MPOFN', 0x03E: 'MPOFR', 0x040: 'MPABN0', 0x042: 'MPCDN0',
    0x070: 'SCXIN0', 0x072: 'SCXDN0', 0x074: 'SCYIN0', 0x076: 'SCYDN0',
    0x078: 'ZMXIN0', 0x07A: 'ZMXDN0', 0x07C: 'ZMYIN0', 0x07E: 'ZMYDN0',
    0x098: 'ZMCTL', 0x09A: 'SCRCTL', 0x09C: 'VCSTAU', 0x09E: 'VCSTAL', 0x0A0: 'LSTA0U', 0x0A2: 'LSTA0L',
    0x0A8: 'LCTAU', 0x0AA: 'LCTAL', 0x0AC: 'BKTAU', 0x0AE: 'BKTAL',
    0x0D0: 'WCTLA', 0x0D2: 'WCTLB', 0x0E0: 'SPCTL', 0x0E2: 'SDCTL', 0x0E4: 'CRAOFA', 0x0E6: 'CRAOFB',
    0x0E8: 'LNCLEN', 0x0EA: 'SFPRMD', 0x0EC: 'CCCTL', 0x0EE: 'SFCCMD',
    0x0F0: 'PRISA', 0x0F2: 'PRISB', 0x0F4: 'PRISC', 0x0F6: 'PRISD', 0x0F8: 'PRINA', 0x0FA: 'PRINB',
    0x108: 'CCRNA', 0x110: 'CLOFEN', 0x112: 'CLOFSL', 0x114: 'COAR', 0x116: 'COAG', 0x118: 'COAB',
}


def name(offset):
    return NAMES.get(offset, f'{offset:03X}')


def load(path):
    with Path(path).open() as handle:
        return [(int(row['field']), int(row['offset'], 16), int(row['old'], 16), int(row['new'], 16))
                for row in csv.DictReader(handle)]


def state_at(rows, field):
    state = {}
    for when, offset, _, new in rows:
        if when <= field:
            state[offset] = new
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('traces', nargs='+', type=Path)
    parser.add_argument('--field', type=int, help='print each register at this field, one column per trace')
    parser.add_argument('--changes-from', type=int, default=0)
    parser.add_argument('--changes-to', type=int)
    parser.add_argument('--only-different', action='store_true')
    args = parser.parse_args()
    traces = [load(path) for path in args.traces]
    if args.field is not None:
        states = [state_at(rows, args.field) for rows in traces]
        for offset in sorted(set().union(*states)):
            values = [state.get(offset, 0) for state in states]
            if args.only_different and len(set(values)) == 1:
                continue
            print(f'{name(offset):8}', *(f'{value:04X}' for value in values))
    if args.changes_to is not None:
        for path, rows in zip(args.traces, traces):
            print(f'-- {path}')
            for when, offset, old, new in rows:
                if args.changes_from <= when <= args.changes_to:
                    print(f'{when:6} {name(offset):8} {old:04X} -> {new:04X}')


if __name__ == '__main__':
    main()
