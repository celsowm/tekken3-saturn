#!/usr/bin/env python3
"""Record a disc in Mednafen with its own movie recorder, keep still frames and a contact sheet, delete the movie.

Needs no window focus or key presses (it works with the screen locked), so a disc that drives itself (the self-test
builds' scripted pads) can be checked from any session. The movie is the cost (about 4 MB per second): it is deleted as
soon as its frames are read (AGENTS.md); use --keep-movie only for a short window you need to look at again.

    scripts/mednafen_record_frames.py build/practice-menu/disc/tekken3_saturn.cue --out build/practice-menu-rec \\
        --seconds 150 --frames 8:150:8

--frames START:STOP:STEP (seconds into the run) or a comma list of seconds. Output: <out>/frames/tNNN.png (352x240 by
default) and <out>/frames/sheet.png. The emulator's output is discarded.
"""
import argparse
import glob
import os
import signal
import subprocess
import time
from pathlib import Path


def parse_times(text):
    if ':' in text:
        start, stop, step = (int(v) for v in text.split(':'))
        return list(range(start, stop + 1, step))
    return [int(v) for v in text.split(',') if v]


def record(cue, movie, seconds, mednafen, extra):
    command = [mednafen, '-sound', '0', '-video.driver', 'softfb', '-qtrecord', str(movie), *extra, str(cue)]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(seconds)
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()


def extract(movie, frames_dir, times, size):
    frames_dir.mkdir(parents=True, exist_ok=True)
    for old in glob.glob(str(frames_dir / '*.png')):
        os.remove(old)
    for second in times:
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', str(second), '-i', str(movie), '-frames:v', '1',
                        '-vf', f'scale={size}', str(frames_dir / f't{second:03d}.png')], check=False)
    return sorted(frames_dir.glob('t*.png'))


def sheet(files, path, columns):
    from PIL import Image
    images = [Image.open(f).convert('RGB') for f in files]
    if not images:
        raise SystemExit('no frames were extracted (the run may be shorter than the requested times)')
    width, height = images[0].size
    rows = (len(images) + columns - 1) // columns
    canvas = Image.new('RGB', (columns * width, rows * height))
    for index, image in enumerate(images):
        canvas.paste(image, ((index % columns) * width, (index // columns) * height))
    canvas.save(path)
    return canvas.size


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('cue', type=Path)
    parser.add_argument('--out', type=Path, required=True, help='a directory; the movie and frames/ go under it')
    parser.add_argument('--seconds', type=int, required=True, help='how long the emulator runs')
    parser.add_argument('--frames', required=True, help='START:STOP:STEP or a comma list of seconds')
    parser.add_argument('--size', default='352:240', help='frame size W:H')
    parser.add_argument('--columns', type=int, default=4)
    parser.add_argument('--mednafen', default='mednafen')
    parser.add_argument('--mednafen-arg', action='append', default=[], help='an extra emulator option')
    parser.add_argument('--keep-movie', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    movie = args.out / 'run.mov'
    if movie.exists():
        raise SystemExit(f'{movie} exists: delete it or use another --out')
    record(args.cue, movie, args.seconds, args.mednafen, args.mednafen_arg)
    try:
        files = extract(movie, args.out / 'frames', parse_times(args.frames), args.size)
        size = sheet(files, args.out / 'frames' / 'sheet.png', args.columns)
        print(f'{len(files)} frames, sheet {size[0]}x{size[1]}: {args.out / "frames" / "sheet.png"}')
    finally:
        if movie.exists() and not args.keep_movie:
            movie.unlink()


if __name__ == '__main__':
    main()
