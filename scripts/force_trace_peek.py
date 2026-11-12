#!/usr/bin/env python3
"""Print chosen RAM values of a recorded game (`flow_trace.py`) for a range of trace frames.

Each value is ADDR[:TYPE] with TYPE one of b (u8), h (s16), H (u16), i (s32, the default), I (u32); an address
may be a fighter record offset written fN+0xOFF (N = 0, 1, 2). Only frames whose ranges hold the address print it.
Run with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/force_trace_peek.py \
        --trace third_party/Tekken3Remake/work/traces/flow_force_run.bin --frames 22030 22040 \
        f0+0x3F4:i 0x800AE204 0x800B70E8
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import check_force_flow_trace as cf   # noqa: E402
import force_sim as fs                # noqa: E402

READ = {'b': 'u8', 'h': 's16', 'H': 'u16', 'i': 's32', 'I': 'u32'}


def parse(spec):
    where, _, kind = spec.partition(':')
    kind = kind or 'i'
    if kind not in READ:
        raise SystemExit(f'{spec}: unknown type {kind}')
    if where.startswith('f') and '+' in where:
        n, off = where[1:].split('+')
        address = (fs.F0, fs.F1, fs.F2)[int(n)] + int(off, 0)
    else:
        address = int(where, 0)
    return spec, address, READ[kind]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--frames', type=int, nargs=2, required=True, metavar=('FROM', 'TO'))
    parser.add_argument('values', nargs='+', help='ADDR[:TYPE] or fN+0xOFF[:TYPE]')
    args = parser.parse_args()
    values = [parse(v) for v in args.values]
    header, body = cf.load_trace(args.trace)
    for k, (pads, ranges, records) in enumerate(cf.frames(header, body)):
        if k > args.frames[1]:
            break
        if k < args.frames[0]:
            continue
        ram = cf.build_ram(header, ranges, records)
        out = []
        for name, address, read in values:
            try:
                out.append(f'{name}={getattr(ram, read)(address)}')
            except Exception:  # the frame does not record this address
                out.append(f'{name}=-')
        print(f'frame {k}: ' + ' '.join(out))


if __name__ == '__main__':
    main()
