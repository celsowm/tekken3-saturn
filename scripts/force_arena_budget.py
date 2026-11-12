#!/usr/bin/env python3
"""Print the Force pack arena's free tail (the pose cache's room) for every player and enemy pairing.

fighter_pack.c loads the player's and the enemy's banks, then both camera streams, into the Low WRAM arena
(T3_ROSTER_ARENA_BYTES), each rounded up to 16 bytes; packs of the same bank type share one bank. With grunts
(t3_fighter_packs_open_force) the arena's last T3_ROSTER_STORE_BYTES hold the third pack, so they are not free.
force_main.c's pose cache takes what is left (init_force_pose_cache). Pairings checked, a superset of what the
game loads: each roster player (F00..F20) against a grunt (F21..F24), against each roster character, and against
each BOSSPAK.BIN costume. Reads the build tree's generated headers and packs; prints only numbers.
"""
import argparse
from pathlib import Path
import re
import struct

DIRECTORY_WORD, BANK, CAMERA = 26, 2, 3


def pack_info(blob, base=0):
    w = struct.unpack_from('>64I', blob, base)
    if w[0] != 0x54334650:
        raise ValueError('not a fighter pack')
    section = lambda s: w[DIRECTORY_WORD + 2 * s + 1]
    return dict(id=w[2], bank_type=w[3], bank=section(BANK), camera=section(CAMERA))


def align(n):
    return n + 15 & ~15


def live(a, b):
    banks = align(a['bank']) + (0 if a['bank_type'] == b['bank_type'] else align(b['bank']))
    return banks + align(a['camera']) + align(b['camera'])


def define(text, name):
    found = re.findall(rf'#define {name} (\d+)u', text)
    return int(found[-1])


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--build', type=Path, default=Path('build/force-work'))
    p.add_argument('--grunts', type=Path, default=Path('assets/imported/force-packs'))
    p.add_argument('--entry-bytes', type=int, default=448, help='sizeof(t3_psx_local_pose_entry_t), at most')
    a = p.parse_args()
    gen = a.build / 'generated'
    headers = (gen / 'fighters/t3_roster.h').read_text() + (gen / 'force-boss/t3_force_boss_limits.h').read_text()
    arena, store = define(headers, 'T3_ROSTER_ARENA_BYTES'), define(headers, 'T3_ROSTER_STORE_BYTES')
    roster = [pack_info((gen / f'fighters/F{i:02}.BIN').read_bytes()) for i in range(21)]
    grunts = [pack_info((a.grunts / f'F{i}.BIN').read_bytes()) for i in range(21, 25)]
    boss_file = (gen / 'force-boss/BOSSPAK.BIN').read_bytes()
    magic, version, count = struct.unpack_from('>3I', boss_file)
    if magic != 0x54334250:
        raise SystemExit('BOSSPAK.BIN: unexpected header')
    costumes = []
    for i in range(count):
        character, costume, base, _ = struct.unpack_from('>4I', boss_file, 12 + 16 * i)
        costumes.append(dict(pack_info(boss_file, base), costume=costume))
    rows = []
    bosses = (a.grunts / 'BOSS.BIN').read_bytes()
    by_key = {(c['id'], c['costume']): c for c in costumes}
    for key in range(min(len(bosses) // 8, 4 * len(roster))):
        player = roster[key // 4]
        for level in range(4):
            character, costume = bosses[8 * key + 2 * level], bosses[8 * key + 2 * level + 1]
            enemy = by_key[(character, costume)] if costume else roster[character]
            rows.append(('BOSS.BIN', player['id'], (character, costume, level), arena - live(player, enemy)))
    for player in roster:
        for grunt in grunts:
            rows.append(('grunt', player['id'], grunt['id'], arena - store - live(player, grunt)))
        for enemy in roster + costumes:
            rows.append(('boss', player['id'], enemy['id'], arena - live(player, enemy)))
    for kind in ('grunt', 'boss', 'BOSS.BIN'):
        some = [r for r in rows if r[0] == kind]
        worst = min(some, key=lambda r: r[3])
        print(f'{kind}: {len(some)} pairings, least free {worst[3]} bytes = {worst[3] // a.entry_bytes} entries '
              f'(player {worst[1]}, enemy {worst[2]})')
    short = sorted((r for r in rows if r[0] != 'boss' and r[3] // a.entry_bytes < 32), key=lambda r: r[3])
    print(f'pairings with room for fewer than 32 entries: {len(short)}', short[:10])


if __name__ == '__main__':
    main()
