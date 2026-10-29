#!/usr/bin/env python3
"""Extract synthetic one-hot texture coverage from a VDP1 probe screenshot."""
import argparse
import json
from pathlib import Path
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('screenshot', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--triangle', action='store_true')
    args = parser.parse_args()
    im = Image.open(args.screenshot).convert('RGB')
    if im.size != (320, 224):
        raise ValueError('expected a 320x224 screenshot')
    cases = []
    for plane in range(16):
        x, y = 10 + plane % 4 * 75, 10 + plane // 4 * 50
        vertices = ([[x, y + 4], [x + 6, y], [x + 3, y + 11], [x + 3, y + 11]]
                    if args.triangle else
                    [[x, y], [x + 32, y + 5], [x + 36, y + 30], [x + 4, y + 25]])
        samples = []
        for ty in range(y // 8, (y + (11 if args.triangle else 30)) // 8 + 1):
            for tx in range(x // 8, (x + (6 if args.triangle else 36)) // 8 + 1):
                mask = [sum(0x80 >> ix for ix in range(8)
                            if im.getpixel((tx * 8 + ix, ty * 8 + iy)) == (248, 0, 248))
                        for iy in range(8)]
                samples.append({'tile': [tx, ty], 'mask': mask})
        if not any(any(s['mask']) for s in samples):
            raise ValueError(f'empty plane {plane}')
        cases.append({'vertices': vertices, 'axis': 'u' if plane < 8 else 'v',
                      'texel': plane % 8, 'samples': samples})
    args.output.write_text(json.dumps({'source': 'authored Ymir one-hot LUT4 coverage probe',
                                      'cases': cases}, separators=(',', ':')) + '\n')
    print(f'{len(cases)} planes, {sum(len(c["samples"]) for c in cases)} tiles')


if __name__ == '__main__':
    main()
