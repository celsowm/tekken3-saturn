#!/usr/bin/env python3
"""Extract every roster character's command list (ARC member 4, USA) as plain text into a private blob and a C source.

The USA move text is decoded by Tekken3Remake's tools/research/move_text.py (pre-rendered words read back by matching their
cells against the ASCII glyphs, so no text is stored in the repository). The blob is

    "T3CL", u8 characters, then per character (by roster id): u16 lines, per line: name NUL command NUL

Output is derived from the game and stays out of git (assets/imported/).

    scripts/extract_command_lists.py --assets assets/imported/practice-menu --out assets/imported/practice-menu/practice-commands
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
import move_text as mt  # noqa: E402
from fighter_roster import ROSTER, records  # noqa: E402


def plain(text):
    """A decoded string without the markers of pre-rendered words, in the ASCII the debug font can draw."""
    text = text.replace('{', '').replace('}', '')
    text = re.sub(r'\s+', ' ', text).strip()
    return ''.join(c if 32 <= ord(c) < 127 else '?' for c in text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--assets', type=Path, required=True, help='prepare_game_assets.py output (SLUS_004.02, raw/)')
    ap.add_argument('--out', type=Path, required=True, help='prefix: writes <out>.bin and <out>.c')
    args = ap.parse_args()
    exe = (args.assets / 'SLUS_004.02').read_bytes()
    blob = bytearray(b'T3CL' + bytes([len(ROSTER)]))
    total = 0
    for fighter in ROSTER:
        lines = []
        path = args.assets / 'raw' / f"{records(fighter)['arc']:03d}.bin"
        if path.is_file():
            members = mt.arc_members(path.read_bytes())
            if len(members) >= 5 and members[4] and members[0].startswith(b'\x10\0\0\0'):
                atlas = mt.Atlas(members[0])
                font = mt.ascii_font(atlas)
                fields = members[4][1:].split(b'\0')[:2 * members[4][0]]
                for name, command in zip(fields[::2], fields[1::2]):
                    lines.append((plain(mt.decode(name, atlas, font, exe)),
                                  plain(mt.decode(command, atlas, font, exe).rstrip(',').replace(',+', '+'))))
        blob += struct.pack('<H', len(lines))
        for name, command in lines:
            blob += name.encode() + b'\0' + command.encode() + b'\0'
        total += len(lines)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.with_suffix('.bin').write_bytes(blob)
    rows = ['/* Derived from the game by scripts/extract_command_lists.py; private, not in git. */',
            '#include <stdint.h>', f'const uint32_t t3_practice_command_blob_size={len(blob)}u;',
            'const uint8_t t3_practice_command_blob[]={']
    for i in range(0, len(blob), 16):
        rows.append(','.join(str(b) for b in blob[i:i + 16]) + ',')
    rows.append('};')
    args.out.with_suffix('.c').write_text('\n'.join(rows) + '\n')
    print(f'{len(ROSTER)} characters, {total} lines, {len(blob)} bytes -> {args.out}.bin/.c')


if __name__ == '__main__':
    main()
