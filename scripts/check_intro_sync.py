#!/usr/bin/env python3
"""Measure how far the intro's sound is from its picture on a Ymir run.

Input: the probe's `--dump-audio` and `--video-observations` of a run of INTRO.BIN. The movie's own sound and
picture are the references:
  * the sound: the envelope of the XA audio ffmpeg decodes (it begins `--audio-lead` s after the first frame, as the
    packer prints) is matched against the envelope of the dump, which gives when movie time 0 is heard;
  * the picture: how many pixels change on each frame of the movie (scene cuts stand out) is matched against how
    many change on each field of the run, which gives the field in which movie frame 0 is first shown.
Prints both and their difference (positive: the sound is late). The emulator's timing, not a console's.
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/intro-probe'
FIELD_HZ = 59.94


def envelope(samples, rate, out_rate=1000):
    mono = np.abs(samples.astype(np.float64)).max(axis=1) if samples.ndim > 1 else np.abs(samples.astype(np.float64))
    step = rate // out_rate
    n = len(mono) // step
    return mono[:n * step].reshape(n, step).mean(axis=1)


def best_lag(ref, probe, max_lag, window):
    """Lag (in samples of both) at which `ref` starts inside `probe`."""
    a = ref[:window] - ref[:window].mean()
    scores = []
    for lag in range(max_lag):
        b = probe[lag:lag + len(a)]
        if len(b) < len(a):
            break
        b = b - b.mean()
        scores.append(float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9)))
    best = int(np.argmax(scores))
    return best, scores[best]


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--audio', type=Path, required=True)
    p.add_argument('--video', type=Path, required=True, help='the probe CSV of --video-observations')
    p.add_argument('--stream', type=Path, default=WORK / 'opening.str')
    p.add_argument('--audio-lead', type=float, default=0.047, help='seconds the movie sound starts after its first frame')
    p.add_argument('--seconds', type=int, default=30)
    p.add_argument('--fps', type=float, default=20.0)
    args = p.parse_args()

    dump = np.fromfile(args.audio, '<i2').reshape(-1, 2)
    ref = subprocess.run(['ffmpeg', '-loglevel', 'error', '-f', 'psxstr', '-i', args.stream, '-map', '0:a:0', '-ar', '22050',
                          '-ac', '2', '-f', 's16le', '-'], check=True, capture_output=True).stdout
    ref = np.frombuffer(ref, '<i2').reshape(-1, 2)
    e_dump, e_ref = envelope(dump, 44100), envelope(ref, 22050)
    lag_ms, corr_a = best_lag(e_ref, e_dump, 8000, args.seconds * 1000)
    audio_zero = lag_ms / 1000 - args.audio_lead

    frames = subprocess.run(['ffmpeg', '-loglevel', 'error', '-f', 'psxstr', '-i', args.stream, '-t', str(args.seconds * 1.4),
                             '-an', '-vf', 'scale=64:48,format=gray', '-fps_mode', 'passthrough', '-f', 'rawvideo', '-'],
                            check=True, capture_output=True).stdout
    frames = np.frombuffer(frames, np.uint8).reshape(-1, 48, 64).astype(np.int16)
    cuts = (np.abs(np.diff(frames, axis=0)) > 24).sum(axis=(1, 2)).astype(np.float64)       # per movie frame
    changed = np.loadtxt(args.video, delimiter=',', skiprows=1, usecols=11)                 # per field
    # Spread the movie's changes over the fields they land in, for a trial field of frame 0.
    nf = int(args.seconds * args.fps)
    best = (-1.0, 0)
    for f0 in range(0, 600):
        idx = np.round(f0 + np.arange(1, nf + 1) * FIELD_HZ / args.fps).astype(int)
        keep = idx < len(changed)
        a, b = cuts[:nf][keep], changed[idx[keep]]
        score = float(np.dot(a - a.mean(), b - b.mean()) / (np.linalg.norm(a - a.mean()) * np.linalg.norm(b - b.mean()) + 1e-9))
        if score > best[0]:
            best = (score, f0)
    video_zero = best[1] / FIELD_HZ
    print(f'sound: movie time 0 is heard at {audio_zero:.3f} s (envelope correlation {corr_a:.3f})')
    print(f'picture: movie frame 0 is shown at {video_zero:.3f} s, field {best[1]} (cut correlation {best[0]:.3f})')
    print(f'the sound is {1000 * (audio_zero - video_zero):+.0f} ms from the picture (positive: late)')


if __name__ == '__main__':
    main()
