#!/usr/bin/env python3
"""Check an active-to-empty ordered-particle transition in Ymir VDP2 maps."""
import argparse
import json
from pathlib import Path
import struct


def read_capture(path):
    observation = json.loads((path / 'observation.json').read_text())
    manifest = json.loads((path / 'symbol-dumps.json').read_text())

    def symbol(name):
        record = manifest['_g_t3_' + name]
        return struct.unpack('>I', (path / record['file']).read_bytes())[0]

    ranges = json.loads((path / 'ymir.json').read_text())['vdp2']['vram_ranges']
    matches = [r['words'] for r in ranges if r['base_word'] == 0x30000 and len(r['words']) == 8192]
    if len(matches) != 1:
        raise ValueError(f'{path}: expected full 64x64 name map')
    words = matches[0]
    names = [(words[i] << 16) | words[i + 1] for i in range(0, len(words), 2)]
    active = {i for i, value in enumerate(names) if value != 0x3200}
    tiles = symbol('particle_swap_tile_count')
    if len(active) != tiles:
        raise ValueError(f'{path}: {len(active)} mapped tiles, published count {tiles}')
    return dict(elf=observation['elf_sha256'], frames=observation['emulator_frames'],
                published=symbol('particle_swap_published'), empty=symbol('mixed_ot_empty_frames'),
                drawn=symbol('ordered_mask_drawn_images'), tiles=tiles, active=active)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('active', type=Path)
    parser.add_argument('empty', type=Path)
    args = parser.parse_args()
    first, last = read_capture(args.active), read_capture(args.empty)
    if first['elf'] != last['elf'] or first['frames'] >= last['frames']:
        raise ValueError('captures must use the same ELF in chronological order')
    if not first['tiles'] or last['tiles'] or last['empty'] <= first['empty']:
        raise ValueError('expected active particles followed by an empty shortcut publication')
    if last['published'] <= first['published'] or last['drawn'] < first['drawn']:
        raise ValueError('publication counters did not advance consistently')
    print(json.dumps({'elf_sha256': first['elf'], 'active_field': first['frames'],
                      'empty_field': last['frames'], 'active_tiles': first['tiles'],
                      'empty_tiles': last['tiles'], 'old_cells_cleared': len(first['active'] - last['active']),
                      'scope': 'VDP2 name map in deterministic Ymir runs; not physical hardware'}, indent=2))


if __name__ == '__main__':
    main()
