#!/usr/bin/env python3
"""Private Force boss costume archive and buffer limits, from the original USA disc.

Default packs are reused. Alternate models/voices follow the costume key and
character-record tables documented by Tekken3Remake (PolyForm Noncommercial;
see NOTICE.md). Required Notice: Copyright (c) 2026 n0madic.
No converted game data belongs in Git.
"""
import argparse
import json
import re
import struct
import tempfile
from pathlib import Path

from build_fighter_packs import build, header, system_textures
from build_stage_packs import usa_file_offset
from fighter_roster import ROSTER


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw', type=Path, required=True)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--roster', type=Path, required=True)
    p.add_argument('--boss-table', type=Path, required=True)
    p.add_argument('--libsaturn-source', type=Path, required=True)
    p.add_argument('--texture-cap', type=int, default=0)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    exe = a.exe.read_bytes()
    bosses = a.boss_table.read_bytes()
    if len(bosses) != 0x5C * 8 or not any(bosses):
        p.error('invalid or placeholder boss table')
    for character, costume in zip(bosses[::2], bosses[1::2]):
        if (character, costume) != (255, 255) and not (character < len(ROSTER) and costume < 4):
            p.error('boss table contains an invalid character/costume')
    wanted = {(c, costume) for c, costume in zip(bosses[::2], bosses[1::2]) if c < len(ROSTER)}
    base_rows = json.loads((a.roster / 'roster.json').read_text())
    images, rows, alternate = {}, list(base_rows), []
    vram = system_textures.build_vram(system_textures.members(exe, 'usa'))
    for character, costume in sorted(wanted):
        if costume == 0:
            images[character, costume] = (a.roster / f'F{character:02}.BIN').read_bytes()
            continue
        key = character * 4 + costume
        slot = exe[usa_file_offset(0x80095CE8) + key]
        ptr = struct.unpack_from('<I', exe, usa_file_offset(0x80098120) + key * 4)[0]
        off = ptr - 0x80010000 + 0x800
        if slot == 0xFF or off < 0 or off + 12 > len(exe):
            p.error(f'missing character/costume {character}/{costume}')
        fighter = ROSTER[character]._replace(slot=slot, voice_set=exe[off+8], bank_type=exe[off+9])
        with tempfile.TemporaryDirectory() as scratch:
            image, _, row = build(fighter, a.raw, exe, vram, a.libsaturn_source,
                                  Path(scratch), a.texture_cap)
        images[character, costume] = image
        rows.append(row)
        alternate.append(dict(row,costume=costume,slot=slot))
    archive = bytearray(b'T3BP0001' + struct.pack('>I', len(images)) + bytes(16 * len(images)))
    for i, ((character, costume), data) in enumerate(sorted(images.items())):
        archive += bytes((-len(archive)) % 16)
        struct.pack_into('>4I', archive, 12 + i * 16, character, costume, len(archive), len(data))
        archive += data
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / 'BOSSPAK.BIN').write_bytes(archive)
    base = dict(re.findall(r'#define (T3_ROSTER_\w+) (\d+)u', (a.roster / 't3_roster.h').read_text()))
    combined = dict(re.findall(r'#define (T3_ROSTER_\w+) (\d+)u', header(rows, 0, [], [], [], [])))
    lines = ['/* Generated private Force-only limits, including boss costumes. */',
             '#ifndef T3_FORCE_BOSS_LIMITS_H', '#define T3_FORCE_BOSS_LIMITS_H']
    for name, value in combined.items():
        if name in base and name != 'T3_ROSTER_COUNT' and int(value) > int(base[name]):
            lines += [f'#undef {name}', f'#define {name} {value}u']
    lines += ['#endif', '']
    (a.out / 't3_force_boss_limits.h').write_text('\n'.join(lines))
    (a.out / 'boss-packs.json').write_text(json.dumps(dict(packs=len(images), bytes=len(archive),
        alternate=alternate), indent=1)+'\n')
    print(f'{len(images)} boss costumes, {len(archive)} bytes')


if __name__ == '__main__':
    main()
