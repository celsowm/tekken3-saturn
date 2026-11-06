#!/usr/bin/env python3
"""Write BOSS.BIN, the Tekken Force boss of every level for every player costume key.

Reads the game's own table through the pinned Tekken3Remake's research port (`force_sim.boss_for`,
verified against force.ovl `FUN_800B2B3C` by `check_force_bosses.py`), so it needs that checkout's
`work/jp_rev1` tree and its venv (unicorn): run it with third_party/Tekken3Remake/work/venv/bin/python.

BOSS.BIN has 0x5C keys x 4 levels x (u8 character, u8 costume), key-major. Character 22 (no
costume of its own in the table) and keys the table does not cover read as 0xFF, 0xFF.
The output is retail-derived: keep it under assets/imported or build/.
"""
import argparse
import sys
from pathlib import Path

RESEARCH = Path(__file__).resolve().parents[1] / 'third_party/Tekken3Remake/tools/research'
KEYS, LEVELS = 0x5C, 4


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(RESEARCH))
    import force_sim as fs
    import verify_menu_sim as vm
    cpu = vm.menu_cpu('force')
    table = bytearray()
    for key in range(KEYS):
        for level in range(LEVELS):
            if key >> 2 == 22:
                table += b'\xff\xff'
                continue
            char, costume = fs.boss_for(cpu, key, level)
            if not (0 <= char < 22 and 0 <= costume < 4):
                raise ValueError(f'key {key:#x} level {level + 1}: boss {char}/{costume}')
            table += bytes((char, costume))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(table))
    print(f'{len(table)} bytes -> {args.out}')


if __name__ == '__main__':
    main()
