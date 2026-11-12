#!/usr/bin/env python3
"""Build a native match evaluator for any two roster fighters, or three for Tekken Force.

t3_match_replay_eval binds its banks and skeletons at compile time. This writes
the private generated sources for one pair, with each fighter's own scale and
body radii, and builds the evaluator against them (never distribute the output).
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_local_kmd_assets import skeleton_parts  # noqa: E402
from fighter_roster import COMMON_RECORD, FORCE_ENEMIES, FORCE_FIRST_ID, ROSTER, bank_record, body_radii, records  # noqa: E402
from kmd_to_ska import parse_kmd  # noqa: E402


def write_character(out, fighters, raw, boss=None):
    models = [parse_kmd((raw / f"{records(f)['kmd']:03}.bin").read_bytes()) for f in fighters]
    radii = ','.join('{' + ','.join(str(b) for b in struct.pack('<8h', *body_radii(f))) + '}' for f in fighters)
    out.mkdir(parents=True, exist_ok=True)
    boss_header = boss_source = ''
    if boss is not None:
        bm = parse_kmd((raw / f"{records(boss)['kmd']:03}.bin").read_bytes())
        brad = ','.join(str(b) for b in struct.pack('<8h', *body_radii(boss)))
        boss_header = (f'#define T3_PRIVATE_BOSS_SCALE {bm.scale_percent}u\n#define T3_PRIVATE_BOSS_RADII {{{brad}}}\n'
                       f'#define T3_PRIVATE_BOSS_CHARACTER {boss.id}u\n#define T3_PRIVATE_BOSS_VOICE {boss.voice_set}u\n'
                       'extern const t3_psx_part_t* const t3_private_boss_parts;\n')
        boss_source = ['static const t3_psx_part_t boss_parts[T3_PSX_DRAW_PARTS] = {'] + \
            ['    {%d,%du,{%d,%d,%d}},' % (p, s, x, y, z) for p, s, (x, y, z) in skeleton_parts(bm)] + \
            ['};', 'const t3_psx_part_t* const t3_private_boss_parts = boss_parts;']
    (out / 't3_generated_character.h').write_text(f"""#ifndef T3_GENERATED_CHARACTER_H
#define T3_GENERATED_CHARACTER_H
#include "psx_pose.h"
#define T3_KMD_SCALE_PERCENT {models[0].scale_percent}u
#define T3_PRIVATE_ACTOR_SCALES {{{','.join(f'{m.scale_percent}u' for m in models)}}}
#define T3_PRIVATE_ACTOR_RADII {{{radii}}}
extern const t3_psx_part_t* const t3_private_actor_parts[{len(models)}];
{boss_header}#endif
""")
    rows = ['#include "t3_generated_character.h"', '/* PRIVATE BUILD ONLY. */']
    for i, model in enumerate(models):
        rows.append(f'static const t3_psx_part_t actor{i}_parts[T3_PSX_DRAW_PARTS] = {{')
        rows += ['    {%d,%du,{%d,%d,%d}},' % (p, s, x, y, z) for p, s, (x, y, z) in skeleton_parts(model)]
        rows.append('};')
    rows.append('const t3_psx_part_t* const t3_private_actor_parts[%d] = {%s};' % (len(models), ','.join(f'actor{i}_parts' for i in range(len(models)))))
    if boss_source:
        rows += boss_source
    (out / 't3_generated_character.c').write_text('\n'.join(rows) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fighters', type=int, nargs='+', required=True, metavar='ID',
                        help='two roster ids, or three for Tekken Force: the player, then two enemy pack ids (21-24)')
    parser.add_argument('--banks', type=int, nargs=2, metavar=('P1', 'P2'),
                        help='fighter ids whose banks they play, when a Mokujin copies another')
    parser.add_argument('--boss', type=int, help='Tekken Force: the roster id of the level\'s boss (its model, radii, scale and voice are generated; its bank is read at run time from T3_FORCE_BOSS_BANK)')
    parser.add_argument('--boss-slot', type=int, help='the boss\'s costume slot')
    parser.add_argument('--boss-bank', type=int, help='the fighter id whose bank the boss plays, when the boss is a Mokujin (default: --boss)')
    parser.add_argument('--player-slot', type=int, help='the player\'s costume slot (its KMD record) when the recording plays another costume than 0')
    parser.add_argument('--assets', type=Path, required=True, help='prepared private import')
    parser.add_argument('--logic', type=Path, required=True, help='directory of NN.t3logic roster banks')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if len(args.fighters) not in (2, 3):
        parser.error('--fighters takes two or three ids')
    fighter_of = lambda i: FORCE_ENEMIES[i - FORCE_FIRST_ID] if i >= FORCE_FIRST_ID else ROSTER[i]
    fighters = [fighter_of(i) for i in args.fighters]
    if args.player_slot is not None:
        fighters[0] = fighters[0]._replace(slot=args.player_slot)
    banks = args.banks or args.fighters[:2]
    tables = args.assets / 'pose-tables'
    logic = args.out / 'generated' / 'private_logic'
    logic.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, str(ROOT / 'scripts' / 'build_local_divmot_assets.py'),
               '--bank', str(args.logic / f'{banks[0]:02}.t3logic'),
               '--common', str(args.assets / 'divmot' / f'{COMMON_RECORD}' / 'bank_99.t3logic'),
               '--sequences', str(args.assets / 'sequences.json'),
               '--spline-tables', str(tables / 'psx_spline.bin'), '--trig-table', str(tables / 'psx_trig.bin'),
               '--ik-tables', str(tables / 'psx_ik.bin'), '--common-branches', str(tables / 'psx_common_branches.bin'),
               '--ground-tables', str(tables / 'psx_ground.bin'), '--body-tables', str(tables / 'psx_body.bin'),
               '--rsqrt-table', str(tables / 'psx_rsqrt.bin'), '--combat-reactions', str(tables / 'psx_reactions.bin'),
               '--ai-tables', str(tables / 'psx_ai.bin'), '--sound-tables', str(tables / 'psx_sounds.bin'),
               '--effect-tables', str(tables / 'psx_effects.bin'), '--output', str(logic)]
    if args.boss is not None:
        command += ['--extra-bank', str(args.logic / f'{args.boss if args.boss_bank is None else args.boss_bank:02}.t3logic')]
    if fighter_of(banks[0]).bank_type != fighter_of(banks[1]).bank_type:
        command += ['--second-bank', str(args.logic / f'{banks[1]:02}.t3logic')]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    write_character(args.out / 'generated' / 'characters', fighters, args.assets / 'raw', (ROSTER[args.boss]._replace(slot=args.boss_slot) if args.boss_slot is not None else ROSTER[args.boss]) if args.boss is not None else None)
    host = args.out / 'host'
    raw = args.assets.resolve() / 'raw'
    configure = ['cmake', '-S', str(ROOT), '-B', str(host), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                 f'-DT3_PRIVATE_REPLAY_ASSET_DIR={args.out.resolve()}', '-DBUILD_TESTING=ON',
                 f'-DT3_LOCAL_CAMERA_BANK={raw / f"{bank_record(fighter_of(banks[0])):03}.bin"}',
                 '-DT3_COMPACT_CAMERA_STREAMS=ON', f'-DT3_LOCAL_CAMERA_SPLINE={tables.resolve() / "psx_spline.bin"}',
                 f'-DT3_LOCAL_CAMERA_EXE={args.assets.resolve() / "SLUS_004.02"}',
                 f'-DT3_PSX_FIGHTERS_COUNT={len(fighters)}']
    if fighter_of(banks[0]).bank_type != fighter_of(banks[1]).bank_type:
        configure.append(f'-DT3_LOCAL_SECOND_CAMERA_BANK={raw / f"{bank_record(fighter_of(banks[1])):03}.bin"}')
    subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
    targets = ['t3_match_replay_eval', 't3_match_replay_camera_eval']
    subprocess.run(['ninja', '-C', str(host), *targets], check=True, stdout=subprocess.DEVNULL)
    for target in targets:
        print(host / target)


if __name__ == '__main__':
    main()
