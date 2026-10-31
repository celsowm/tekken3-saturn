#!/usr/bin/env python3
"""Compare two saturn_probe profile summaries, function by function."""
import argparse
import json
from pathlib import Path


def cycles(path, cpu):
    functions = json.loads(path.read_text())['cpus'][cpu]['functions']
    return {f['symbol']: f['execution_cycles'] for f in functions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('other', type=Path)
    parser.add_argument('--cpu', default='master', choices=('master', 'slave'))
    parser.add_argument('--top', type=int, default=40)
    parser.add_argument('--base-images', type=float, default=1, help='divide base cycles by this')
    parser.add_argument('--other-images', type=float, default=1, help='divide other cycles by this')
    args = parser.parse_args()
    base, other = cycles(args.base, args.cpu), cycles(args.other, args.cpu)
    rows = []
    for symbol in base.keys() | other.keys():
        a = base.get(symbol, 0) / args.base_images
        b = other.get(symbol, 0) / args.other_images
        rows.append((b - a, a, b, symbol))
    rows.sort(key=lambda row: -abs(row[0]))
    print(f'{"delta":>10} {"base":>10} {"other":>10}  symbol')
    for delta, a, b, symbol in rows[:args.top]:
        print(f'{delta:>10.0f} {a:>10.0f} {b:>10.0f}  {symbol[:90]}')


if __name__ == '__main__':
    main()
