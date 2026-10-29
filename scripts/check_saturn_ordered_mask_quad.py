#!/usr/bin/env python3
"""Check mixed particle/face ordering against an independent Ymir quad capture."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    root = args.capture
    observed = json.loads((root / 'observation.json').read_text())
    if observed['state']['first_error']:
        raise ValueError('guest reported an error')
    manifest = json.loads((root / 'symbol-dumps.json').read_text())

    def counter(name):
        item = manifest['_g_t3_' + name]
        return int.from_bytes((root / item['file']).read_bytes(), 'big')

    index8, lut4 = counter('ordered_mask_index8_tiles'), counter('ordered_mask_lut4_tiles')
    if min(index8, lut4) == 0 or counter('particle_swap_tile_count') != 20:
        raise ValueError('missing texture formats or published tiles')
    ranges = json.loads((root / 'ymir.json').read_text())['vdp2']['vram_ranges']

    def words(base, length):
        matches = [r['words'] for r in ranges if r['base_word'] == base and len(r['words']) == length]
        if len(matches) != 1:
            raise ValueError('missing or duplicate VRAM range')
        return matches[0]

    fixture = json.loads((Path(__file__).resolve().parent.parent / 'tests/fixtures/vdp1_parallelogram_masks.json').read_text())
    case = next(c for c in fixture['cases'] if c['vertices'] == [[20, 70], [52, 75], [56, 100], [24, 95]])
    masks = {tuple(s['tile']): s['mask'] for s in case['samples']}
    names = [0, 0x3200] * 4096
    expected = []
    for ty in range(8, 13):
        for tx in range(3, 7):
            names[(ty * 64 + tx) * 2 + 1] = 0x3204 + 4 * ((ty - 8) * 4 + tx - 3)
            for y in range(8):
                for x in range(8):
                    px, py = tx * 8 + x, ty * 8 + y
                    if 24 <= px < 56 and 70 <= py < 100:
                        opaque = masks[(tx, ty)][y] & (0x80 >> x)
                        expected.append(0x8080 if opaque else 0x8084)
                    else:
                        expected.append(0)
    if words(0x30000, 8192) != names:
        raise ValueError('tile-name map differs from the expected 20 tiles')
    actual = words(0x32040, len(expected))
    if actual != expected:
        i = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
        raise ValueError(f'pixel {i}: got {actual[i]:04x}, expected {expected[i]:04x}')
    print(json.dumps({'elf_sha256': observed['elf_sha256'], 'index8_masks': index8,
                      'lut4_masks': lut4, 'map_words': len(names), 'pixels': len(expected),
                      'published': counter('particle_swap_published')}, indent=2))


if __name__ == '__main__':
    main()
