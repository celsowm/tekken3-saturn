#!/usr/bin/env python3
"""Validate native particle/triangle/particle composition against Ymir masks."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    root = args.capture
    observation = json.loads((root / 'observation.json').read_text())
    if observation['state']['first_error']:
        raise ValueError('guest reported an error')
    manifest = json.loads((root / 'symbol-dumps.json').read_text())

    def counter(name):
        return int.from_bytes((root / manifest['_g_t3_' + name]['file']).read_bytes(), 'big')

    if min(counter('ordered_mask_index8_tiles'), counter('ordered_mask_lut4_tiles'),
           counter('ordered_general_tiles')) == 0 or counter('particle_swap_tile_count') != 4:
        raise ValueError('expected both texture formats and four general-mask tiles')
    ranges = json.loads((root / 'ymir.json').read_text())['vdp2']['vram_ranges']

    def words(base, length):
        found = [r['words'] for r in ranges if r['base_word'] == base and len(r['words']) == length]
        if len(found) != 1:
            raise ValueError('missing or duplicate VRAM range')
        return found[0]

    cases = json.loads((ROOT / 'tests/fixtures/vdp1_triangle_planes.json').read_text())['cases']
    face = next(c for c in cases if c['axis'] == 'u' and c['texel'] == 3)
    dx, dy = 30 - face['vertices'][0][0], 22 - face['vertices'][0][1]
    mask = set()
    for sample in face['samples']:
        tx, ty = sample['tile']
        for y, row in enumerate(sample['mask']):
            for x in range(8):
                if row & (0x80 >> x):
                    mask.add((tx * 8 + x + dx, ty * 8 + y + dy))
    names = [0, 0x3200] * 4096
    expected = []
    for ty in range(2, 4):
        for tx in range(3, 5):
            names[(ty * 64 + tx) * 2 + 1] = 0x3204 + 4 * ((ty - 2) * 2 + tx - 3)
            for y in range(8):
                for x in range(8):
                    expected.append(0x8080 if (tx * 8 + x, ty * 8 + y) in mask else 0x8084)
    if words(0x30000, 8192) != names or words(0x32040, 256) != expected:
        raise ValueError('published map or pixel words differ from reference')
    print(json.dumps({'elf_sha256': observation['elf_sha256'], 'general_tiles': counter('ordered_general_tiles'),
                      'index8': counter('ordered_mask_index8_tiles'), 'lut4': counter('ordered_mask_lut4_tiles'),
                      'pixels': len(expected), 'published': counter('particle_swap_published')}, indent=2))


if __name__ == '__main__':
    main()
