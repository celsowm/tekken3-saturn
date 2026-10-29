#!/usr/bin/env python3
"""Compare stage mapping from matched Ymir control and candidate frames."""
import argparse
import json
from pathlib import Path
from PIL import Image


def symbol(root, name):
    manifest = json.loads((root / 'symbol-dumps.json').read_text())
    return (root / manifest['_' + name]['file']).read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('control', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    for root in (args.control, args.candidate):
        observed = json.loads((root / 'observation.json').read_text())
        if observed['state']['first_error']:
            raise ValueError('guest reported an error')
    if symbol(args.control, 'g_previous_camera') != symbol(args.candidate, 'g_previous_camera'):
        raise ValueError('camera states differ')
    original = symbol(args.control, 'g_params')
    shifted = symbol(args.candidate, 'g_params')
    if len(original) != 88 or len(shifted) != 88 or original[4:] != shifted[4:]:
        raise ValueError('unexpected rotation parameter difference')
    x0 = int.from_bytes(original[:4], 'big', signed=True)
    x1 = int.from_bytes(shifted[:4], 'big', signed=True)
    if x1 - x0 != -7 * 65536:
        raise ValueError('incorrect floor origin')
    before = Image.open(args.control / 'screen.png').convert('RGB')
    after = Image.open(args.candidate / 'screen.png').convert('RGB')
    if before.size != (320, 224) or after.size != before.size:
        raise ValueError('unexpected screenshot dimensions')
    for y in (37, 39, 210, 215):
        for x in range(20, 300):
            if before.getpixel((x - 7, y)) != after.getpixel((x, y)):
                raise ValueError(f'stage pixel differs at ({x},{y})')
    print(json.dumps({'floor_delta_16_16': x1 - x0, 'matched_stage_pixels': 4 * 280,
                      'camera_bytes_match': True}, indent=2))


if __name__ == '__main__':
    main()
