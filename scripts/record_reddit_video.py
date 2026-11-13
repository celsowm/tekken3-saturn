#!/usr/bin/env python3
"""Record a showcase video: boot intro, title, menus, loading and a 1P-vs-CPU fight, as a small MP4 with sound.

Mednafen records the real run (-qtrecord, lossless, about 3-6 MB/s); keys are sent to its process with
scripts/mednafen_input_script.swift (needs accessibility access); ffmpeg then encodes H.264/AAC and the raw movie is
deleted at once (AGENTS.md). Port 2 is empty, so the build hands that fighter to the CPU. The times are seconds
after launch and depend on the emulator, so every one is an option; look at the result and adjust.
"""
import argparse
import random
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# macOS virtual key codes of the keys bound in ~/.mednafen/mednafen.cfg (scripts/configure_mednafen_controls.py)
RETURN, LEFT, RIGHT, DOWN, UP = 36, 123, 124, 125, 126
PUNCH_X, PUNCH_Y, KICK_A, KICK_B, JUMP_C, GUARD = 0, 1, 6, 7, 8, 12


def input_script(args):
    # START skips the movie, leaves the title and confirms ARCADE MODE: one press short and an idle menu falls into the
    # CPU demo. The select cursors begin on Paul and Law, the pair of the first demo, so they move before each pick.
    lines = [f'{at} {RETURN} 0.15' for at in (args.skip_movie, args.title_start)]
    # TEKKEN FORCE MODE is five steps down from ARCADE MODE (scripts/menu_flow.py --down 5).
    lines += [f'{args.arcade + 0.4 * i:.2f} {DOWN} 0.1' for i in range(5 if args.force_mode else 0)]
    lines.append(f'{args.arcade + (2.2 if args.force_mode else 0):.2f} {RETURN} 0.15')
    for pick, moves in (((args.pick_p1, args.p1_moves),) if args.ladder or args.force_mode else
                        ((args.pick_p1, args.p1_moves), (args.pick_cpu, args.cpu_moves))):
        key = RIGHT if moves > 0 else LEFT
        lines += [f'{pick - 0.5 * (abs(moves) - i):.2f} {key} 0.1' for i in range(abs(moves))]
        lines.append(f'{pick} {RETURN} 0.15')
    rng, at = random.Random(3), args.fight_start
    while at < args.seconds - 2:
        lines.append(f'{at:.2f} {RIGHT} {rng.uniform(0.3, 0.9):.2f}')
        at += rng.uniform(0.5, 1.1)
        for _ in range(rng.randint(2, 4)):
            lines.append(f'{at:.2f} {rng.choice((PUNCH_X, PUNCH_Y, KICK_A, KICK_B))} 0.08')
            at += rng.uniform(0.25, 0.5)
        # Force walks a level from left to right: no step back.
        move = rng.choice((GUARD, JUMP_C, DOWN) if args.force_mode else (GUARD, JUMP_C, DOWN, LEFT))
        lines.append(f'{at:.2f} {move} {rng.uniform(0.2, 0.6):.2f}')
        at += rng.uniform(0.5, 1.0)
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc', type=Path, default=ROOT / 'build/roster/disc/music.cue')
    p.add_argument('--out', type=Path, default=ROOT / 'build/reddit/tekken3-saturn-v0.2.mp4')
    p.add_argument('--seconds', type=float, default=230)
    p.add_argument('--skip-movie', type=float, default=122, help='START during the opening movie')
    p.add_argument('--title-start', type=float, default=125, help='START at the title')
    p.add_argument('--arcade', type=float, default=129, help='START on ARCADE MODE')
    # The select screen is up from about 151 s to 182 s (20 timer units); earlier keys fall into NOW LOADING and are lost.
    p.add_argument('--pick-p1', type=float, default=158, help='START on the first fighter')
    p.add_argument('--pick-cpu', type=float, default=164, help='START on the CPU fighter')
    p.add_argument('--p1-moves', type=int, default=-2, help='cursor steps from Paul; the default gives Hwoarang')
    p.add_argument('--cpu-moves', type=int, default=4, help='cursor steps from Law; the default gives King')
    p.add_argument('--fight-start', type=float, default=195, help='first fight input')
    p.add_argument('--trim', type=float, default=0, help='seconds to cut from the start of the encode')
    p.add_argument('--keep-raw', action='store_true')
    p.add_argument('--inputs', type=Path, help='a key script of "SECONDS KEYCODE HOLD" lines, used instead of the generated one')
    p.add_argument('--ladder', action='store_true', help='V0.3 arcade mode: only P1 picks, the CPU opponent is drawn')
    p.add_argument('--force-mode', action='store_true',
                   help='V0.4 Tekken Force: --arcade starts the five DOWN presses to TEKKEN FORCE MODE, then START; only P1 picks')
    args = p.parse_args()
    work = args.out.parent
    work.mkdir(parents=True, exist_ok=True)
    raw, script = work / 'raw.mov', work / 'inputs.txt'
    script.write_text(args.inputs.read_text() if args.inputs else input_script(args))
    cmd = ['mednafen', '-sound', '1', '-ss.correct_aspect', '0', '-ss.input.port2', 'none', '-qtrecord', str(raw), str(args.disc)]
    with (work / 'mednafen.log').open('w') as log:
        game = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
        time.sleep(1.5)
        keys = subprocess.Popen(['swift', str(ROOT / 'scripts/mednafen_input_script.swift'), str(game.pid), str(script)],
                                stdout=log, stderr=subprocess.STDOUT)
        try:
            game.wait(timeout=args.seconds)
            keys.kill()
            raise SystemExit(f'Mednafen closed before {args.seconds} s; nothing encoded, {raw} kept')
        except subprocess.TimeoutExpired:
            game.send_signal(signal.SIGINT)
            game.wait(timeout=30)
        keys.wait(timeout=30)
    trim = ['-ss', str(args.trim)] if args.trim else []
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', *trim, '-i', str(raw), '-vf', 'scale=960:720:flags=lanczos,setsar=1',
                    '-c:v', 'libx264', '-crf', '20', '-preset', 'slow', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '160k',
                    '-movflags', '+faststart', str(args.out)], check=True)
    if not args.keep_raw:
        raw.unlink()
    print(f'{args.out}: {args.out.stat().st_size / 1048576:.1f} MiB')


if __name__ == '__main__':
    main()
