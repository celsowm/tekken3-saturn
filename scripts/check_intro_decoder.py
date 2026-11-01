#!/usr/bin/env python3
"""Check LibSaturn's movie decoder, picture and sound, against ffmpeg on the real movie.

Builds tools/movie_dump.cpp against LibSaturn's decoder, decodes the whole packed movie (so every
frame depends on the ones before it, as on the console), writes a few frames and compares them with
ffmpeg's own decode of the same Cinepak file, reduced to 5 bits a channel. Needs the files that
`scripts/pack_intro_video.py build` leaves in build/intro-probe. Prints the share of identical dots and
the mean difference in 5-bit steps; exits 1 above the limits.
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/intro-probe'


def check_audio(args, out, lib):
    """The decoded IMA sound against ffmpeg's decode of the same XA stream: same length, and close in level."""
    tool = out / 'movie_audio_dump'
    subprocess.run(['c++', '-std=c++20', '-O1', '-I', lib / 'include', '-o', tool, ROOT / 'tools/movie_audio_dump.cpp',
                    lib / 'src/audio/playback/ima.cpp', '-iquote', lib], check=True)
    pcm = out / 'sound.pcm'
    subprocess.run([tool, args.movie, pcm], check=True)
    mine = np.fromfile(pcm, '<i2').reshape(-1, 2).astype(np.float64)
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'psxstr', '-i', WORK / 'opening.str', '-map', '0:a:0',
                    '-ar', '22050', '-ac', '2', '-f', 's16le', out / 'ref.pcm'], check=True)
    ref = np.fromfile(out / 'ref.pcm', '<i2').reshape(-1, 2).astype(np.float64)
    # Find where the movie's sound starts inside ffmpeg's: the lead silence the packer prepends is at most 100 ms.
    def correlation(lag):
        n = min(len(mine) - lag, len(ref), 441000)
        a, b = mine[lag:lag + n, 0], ref[:n, 0]
        return float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9))
    coarse = max(range(0, 4410, 11), key=correlation)
    best = max((correlation(lag), lag) for lag in range(max(0, coarse - 11), coarse + 12))
    corr, lag = best
    bad = corr < 0.95
    print(f'sound: {len(mine) / 22050:.1f} s decoded, correlation with ffmpeg {corr:.4f} at {lag / 22.05:.0f} ms'
          + ('  FAIL' if bad else ''))
    return int(bad)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--lib', type=Path, default=ROOT.parent / 'lib-saturn')
    p.add_argument('--movie', type=Path, default=WORK / 'INTRO.MOV')
    p.add_argument('--avi', type=Path, default=WORK / 'op-240x168.avi')
    p.add_argument('--frames', type=int, nargs='+', default=[0, 1, 300, 900, 1500, 1949])
    p.add_argument('--min-exact', type=float, default=0.97)
    p.add_argument('--max-mean', type=float, default=0.05)
    args = p.parse_args()
    out = WORK / 'check'
    out.mkdir(parents=True, exist_ok=True)
    tool = out / 'movie_dump'
    subprocess.run(['c++', '-std=c++20', '-O1', '-I', args.lib / 'include', '-o', tool,
                    ROOT / 'tools/movie_dump.cpp', args.lib / 'src/graphics/movie/cinepak.cpp'], check=True)
    subprocess.run([tool, args.movie, out, *map(str, args.frames)], check=True)
    header = args.movie.read_bytes()[:12]
    width, height = int.from_bytes(header[8:10], 'big'), int.from_bytes(header[10:12], 'big')
    status = 0
    status |= check_audio(args, out, lib=args.lib)
    for n in args.frames:
        mine = np.fromfile(out / f'frame-{n:05d}.rgb', np.uint8).reshape(height, width, 3) >> 3
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', args.avi, '-fps_mode', 'passthrough', '-vf', f'select=eq(n\\,{n})',
                        '-frames:v', '1', '-pix_fmt', 'rgb24', '-f', 'rawvideo', out / 'ref.rgb'], check=True)
        ref = np.fromfile(out / 'ref.rgb', np.uint8).reshape(height, width, 3) >> 3
        diff = np.abs(mine.astype(int) - ref.astype(int))
        exact, mean = float((diff == 0).all(axis=2).mean()), float(diff.mean())
        bad = exact < args.min_exact or mean > args.max_mean
        status |= bad
        print(f'frame {n:5d}: {exact * 100:6.2f}% dots identical, mean difference {mean:.4f} steps'
              + ('  FAIL' if bad else ''))
    sys.exit(status)


if __name__ == '__main__':
    main()
