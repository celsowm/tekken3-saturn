#!/usr/bin/env python3
"""Record a showcase video on Ymir from an exact pad script: the frames and the sound of one deterministic run.

Mednafen takes (scripts/record_reddit_video.py) send keys by the wall clock, and on long runs some never reach the
game. Here the Ymir probe replays a pad script by program field (injection boot of the full disc, as
scripts/menu_flow.py does), keeps a screenshot every --step fields and the SCSP output (--dump-audio), and ffmpeg
encodes them; the screenshots and the raw audio are deleted at once (AGENTS.md).

The generated route is the full disc's: START at the title, --down presses on the mode list (5: TEKKEN FORCE MODE),
--right presses on the select and a pick, then from --fight-from a seeded pattern of walking right and attacks.
--pad uses a script of "FIELD BUTTONS" lines instead. The fields depend on disc read speed; look at the result.
Injection boot has no BIOS backup library, so a Force run shows PROGRESS NOT SAVED.

    scripts/record_ymir_video.py --build build/saturn --out build/reddit/force-ymir.mp4 --down 5 --frames 12000
"""
import argparse
import random
import re
import shutil
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
W, H = 960, 720
RATE = 44100
FPS = 30
# The probe logs each shot with the stereo samples of --dump-audio recorded so far: program fields are not evenly
# spaced in time across disc loads, the sound is, so each shot is placed on the sound's timeline.
SHOT = re.compile(r'^\[probe\] frame (\d+) -> \S+ \(\d+x\d+\) audio (\d+)$', re.M)


def route(args):
    script = ['0 NONE', f'{args.title_at} START', f'{args.title_at + 6} NONE']
    at = args.title_at + 80
    for _ in range(args.down):
        script += [f'{at} DOWN', f'{at + 6} NONE']
        at += 20
    script += [f'{at} START', f'{at + 6} NONE']
    at = args.pick_at - 30 * args.right
    for _ in range(args.right):
        script += [f'{at} RIGHT', f'{at + 6} NONE']
        at += 30
    script += [f'{args.pick_at} START', f'{args.pick_at + 6} NONE']
    rng, at = random.Random(args.seed), args.fight_from
    while at < args.frames:
        walk = rng.randint(30, 90)
        script.append(f'{at} RIGHT')
        at += walk
        for _ in range(rng.randint(2, 5)):
            script += [f'{at} {rng.choice(("X", "Y", "A", "B", "RIGHT+X", "RIGHT+A", "DOWN+B"))}', f'{at + 4} NONE']
            at += rng.randint(14, 26)
        if rng.random() < 0.3:
            script += [f'{at} {rng.choice(("L", "C", "DOWN"))}', f'{at + rng.randint(10, 30)} NONE']
            at += 36
    return '\n'.join(script) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build/saturn')
    p.add_argument('--probe', type=Path, default=ROOT.parent / 'lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=12000, help='program fields to run')
    p.add_argument('--from-field', type=int, default=0, help='first field in the video')
    p.add_argument('--step', type=int, default=2, help='fields per video frame')
    p.add_argument('--pad', type=Path, help='a pad script used instead of the generated route')
    p.add_argument('--title-at', type=int, default=2250)
    p.add_argument('--down', type=int, default=0, help='DOWN presses on the mode list (5: TEKKEN FORCE MODE)')
    p.add_argument('--right', type=int, default=0, help='RIGHT presses on the select before the pick')
    p.add_argument('--pick-at', type=int, default=3300, help='START on the select')
    p.add_argument('--fight-from', type=int, default=6300, help='first fight input')
    p.add_argument('--seed', type=int, default=3)
    args = p.parse_args()
    work = args.out.parent / f'{args.out.stem}-frames'
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    pad = args.out.with_suffix('.pad')
    pad.write_text(args.pad.read_text() if args.pad else route(args))
    audio = work / 'audio.s16'
    fields = range(args.from_field, args.frames, args.step)
    cmd = [str(args.probe), '--iso', str(args.build / 'disc/music.cue'), '--bin', str(args.build / 'tekken3_saturn.app.bin'),
           '--bios', str(args.bios), '--frames', str(args.frames), '--out', str(work / 'run.json'),
           '--pad-script', str(pad), '--dump-audio', str(audio)]
    for field in fields:
        cmd += ['--screenshot', f'{field}:{work / f"{field:06}.png"}']
    try:
        run = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if run.returncode:
            raise SystemExit(f'probe failed ({run.returncode}):\n' + run.stderr[-2000:])
        shots = [(int(sample) / RATE, int(field)) for field, sample in SHOT.findall(run.stderr)]
        if not shots:
            raise SystemExit('no screenshots logged with their audio position (rebuild the LibSaturn probe)')
        start, end = shots[0][0], audio.stat().st_size / 4 / RATE
        encode = subprocess.Popen(
            ['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}',
             '-r', str(FPS), '-i', '-', '-f', 's16le', '-ar', str(RATE), '-ac', '2', '-ss', f'{start:.4f}',
             '-i', str(audio), '-c:v', 'libx264', '-crf', '20', '-preset', 'medium', '-pix_fmt', 'yuv420p',
             '-c:a', 'aac', '-b:a', '160k', '-shortest', '-movflags', '+faststart', str(args.out)],
            stdin=subprocess.PIPE)
        frames, k, image, shown = 0, 0, None, None
        while start + frames / FPS < end:
            t = start + frames / FPS
            while k + 1 < len(shots) and shots[k + 1][0] <= t:
                k += 1
            if shown != k:
                image = Image.open(work / f'{shots[k][1]:06}.png').convert('RGB').resize((W, H), Image.LANCZOS)
                shown = k
            encode.stdin.write(image.tobytes())
            frames += 1
        encode.stdin.close()
        if encode.wait():
            raise SystemExit('ffmpeg failed')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f'{args.out}: {args.out.stat().st_size / 1048576:.1f} MiB, {frames / FPS:.1f} s from {len(shots)} shots')


if __name__ == '__main__':
    main()
