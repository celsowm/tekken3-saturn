#!/usr/bin/env python3
"""Run the same Ymir fight with two stage bakes and compare them.

--control and --candidate are build_local_paul_stage.py output folders. Each
bake's PAUL.STG and manifest go into the build, the disc is rebuilt in place
and the fight runs with screenshots. The report says whether the program,
images, fields and AI hash match, and which screen rows changed. The candidate
stays installed afterwards. Screenshots and report.json go to --out.
"""
import argparse
import filecmp
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from compare_images import difference, stacked_view

ROOT = Path(__file__).resolve().parents[1]


def install(bake, build):
    generated = build/'generated/private_stage'
    if not filecmp.cmp(bake/'t3_local_stage.h', generated/'t3_local_stage.h', shallow=False):
        raise ValueError(f'{bake} needs another generated header; this comparison only swaps the picture')
    # A fresh file date, so that the build sees the change and rebuilds the disc.
    for name in ('PAUL.STG', 'manifest.json'):
        shutil.copyfile(bake/name, generated/name)
    subprocess.run(['cmake', '--build', str(build), '-j8'], check=True, stdout=subprocess.DEVNULL)
    if not filecmp.cmp(bake/'PAUL.STG', build/'disc/tekken3_saturn.iso_root/PAUL.STG', shallow=False):
        raise ValueError(f'the disc did not take the picture of {bake}')
    return hashlib.sha256((build/'tekken3_saturn.elf').read_bytes()).hexdigest()


def fight(label, args):
    probe = ROOT/f'assets/imported/stage-bake-ab-{label}'
    shutil.rmtree(probe, ignore_errors=True)
    command = [sys.executable, str(ROOT/'scripts/saturn_probe.py'), '--build', str(args.build),
               '--disc', str(args.build/'disc/music.cue'), '--bios', str(args.bios), '--out', str(probe),
               '--frames', str(args.fields), '--timeout', '500', '--port2', 'none', '--sh2-cache', 'on']
    for field in args.shot:
        command += ['--screenshot-frame', str(field)]
    try:
        run = json.loads(subprocess.check_output(command, text=True))
        for field in args.shot:
            shutil.copy2(probe/f'frame-{field}.png', args.out/f'{label}-{field}.png')
    finally:
        shutil.rmtree(probe, ignore_errors=True)
    state = run['state']
    return dict(images=state['submitted_frames'], observed_fields=state['observed_fields'],
                images_per_second=round(state['submitted_frames']*60/state['observed_fields'], 2),
                discarded_fields=state['discarded_fields'],
                ai_pad_hash=hex(run['optimizations'].get('ai_pad_hash', 0)))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--control', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--build', type=Path, default=ROOT/'build/roster')
    p.add_argument('--fields', type=int, default=6000)
    p.add_argument('--shot', type=int, action='append', default=[], help='program field to screenshot')
    p.add_argument('--bios', type=Path, default=ROOT/'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    args = p.parse_args()
    args.shot = args.shot or [3000, 4500]
    args.out.mkdir(parents=True, exist_ok=True)
    report = {}
    try:
        for label, bake in (('control', args.control), ('candidate', args.candidate)):
            elf = install(bake, args.build)
            report[label] = dict(elf_sha256=elf, **fight(label, args))
    finally:
        install(args.candidate, args.build)
    report['same_run'] = all(report['control'][key] == report['candidate'][key]
                             for key in ('elf_sha256', 'images', 'observed_fields', 'ai_pad_hash'))
    report['screens'] = {}
    for field in args.shot:
        control, candidate = (args.out/f'{label}-{field}.png' for label in ('control', 'candidate'))
        report['screens'][field] = difference(control, candidate)
        stacked_view(control, candidate, args.out/f'view-{field}.png')
    (args.out/'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
