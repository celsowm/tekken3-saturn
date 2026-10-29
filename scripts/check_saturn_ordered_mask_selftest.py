#!/usr/bin/env python3
"""Check the synthetic particle/face/particle publication in Ymir VRAM."""
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
    if min(index8, lut4) == 0 or counter('particle_swap_tile_count') != 1:
        raise ValueError('missing texture formats or published tile')
    ranges = json.loads((root / 'ymir.json').read_text())['vdp2']['vram_ranges']
    def words(base, length):
        matches = [r['words'] for r in ranges if r['base_word'] == base and len(r['words']) == length]
        if len(matches) != 1:
            raise ValueError('missing or duplicate VRAM range')
        return matches[0]
    names = words(0x30000, 8192)
    expected_names = [0, 0x3200] * 4096
    expected_names[(8 * 64 + 10) * 2 + 1] = 0x3204
    if names != expected_names:
        raise ValueError('tile-name map differs from the single expected tile')
    # First particle adds red; checkerboard face clears it; second adds green.
    expected = [0x8080 if (x + y) & 1 else 0x8084 for y in range(8) for x in range(8)]
    if words(0x32040, 64) != expected:
        raise ValueError('published pixel colors do not match painter-order composition')
    print(json.dumps({'elf_sha256': observed['elf_sha256'], 'index8_masks': index8,
                      'lut4_masks': lut4, 'map_words': len(names), 'pixels': len(expected),
                      'published': counter('particle_swap_published')}, indent=2))

if __name__ == '__main__':
    main()
