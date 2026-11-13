#!/usr/bin/env python3
"""Pick the scripted Force run worth recording: run scripts/record_ymir_video.py's route for several seeds on Ymir,
without screenshots, and read the level runner's kills, score, timer and the player's health at --at.

The fields are read from the Force executable's g_owned_match.force (t3_force_level_t in src/game/psx_force_level.h;
offsets from an offsetof probe of the Saturn build). A run that has left the level by --at reads another program's
memory, so its level must be 0..4 and its timer plausible to count. Prints one row per seed, best first.

    scripts/ymir_force_seed_search.py --seeds 1-24 --at 8000 --down 5 --right 2 --pick-at 3300
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from record_ymir_video import ROOT, route  # noqa: E402

FORCE = 0x54  # offsetof(t3_psx_match_t, force)
LEVEL, SCORE, TIMER, HEALTH, KILLS = 0x00, 0x18, 0x20, 0x74, 0x8E


def nm(elf, name):
    out = subprocess.check_output([str(Path.home() / 'dev/saturn-tools/bin/sh2eb-elf-nm'), str(elf)], text=True)
    return next(int(line.split()[0], 16) for line in out.splitlines() if line.endswith(' ' + name))


def run(args, base, seed, work):
    args.seed = seed
    pad, dump = work / f'{seed}.pad', work / f'{seed}.bin'
    pad.write_text(route(args))
    subprocess.run([str(args.probe), '--iso', str(args.build / 'disc/music.cue'), '--bin',
                    str(args.build / 'tekken3_saturn.app.bin'), '--bios', str(args.bios), '--frames', str(args.at),
                    '--out', str(work / f'{seed}.json'), '--pad-script', str(pad), '--dump-mem',
                    f'0x{base + FORCE:08x}:{KILLS + 2}:{dump}'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    data = dump.read_bytes()
    level, = struct.unpack_from('>i', data, LEVEL)
    score, = struct.unpack_from('>I', data, SCORE)
    timer, = struct.unpack_from('>i', data, TIMER)
    health, = struct.unpack_from('>i', data, HEALTH)
    kills, = struct.unpack_from('>H', data, KILLS)
    valid = 0 <= level <= 4 and 0 <= timer < 100 * 60 and score < 10_000_000
    return seed, valid, kills, score, health, timer


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build/saturn')
    p.add_argument('--probe', type=Path, default=ROOT.parent / 'lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--seeds', default='1-16', help='FIRST-LAST')
    p.add_argument('--at', type=int, default=8000, help='field at which the level is read')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--skip-at', type=int, default=1700)
    p.add_argument('--title-at', type=int, default=2250)
    p.add_argument('--down', type=int, default=5)
    p.add_argument('--right', type=int, default=0)
    p.add_argument('--pick-at', type=int, default=3300)
    p.add_argument('--fight-from', type=int, default=6300)
    args = p.parse_args()
    args.frames = args.at
    first, last = (int(x) for x in args.seeds.split('-'))
    base = nm(args.build / 'tekken3_force.elf', '_g_owned_match')
    with tempfile.TemporaryDirectory() as folder, ThreadPoolExecutor(args.jobs) as pool:
        rows = list(pool.map(lambda s: run(argparse.Namespace(**vars(args)), base, s, Path(folder)),
                             range(first, last + 1)))
    rows.sort(key=lambda r: (r[1], r[2], r[3], r[4]), reverse=True)
    for seed, valid, kills, score, health, timer in rows:
        print(f'seed {seed:3d} {"in level" if valid else "left    "} kills {kills:3d} score {score:7d} '
              f'health {health} timer {timer}')


if __name__ == '__main__':
    main()
