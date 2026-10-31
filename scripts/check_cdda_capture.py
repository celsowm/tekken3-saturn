#!/usr/bin/env python3
"""Compare a short captured audio window with a local CD-DA source recording."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np


def decode(path, start, seconds, rate):
    command = ['ffmpeg', '-v', 'error', '-ss', str(start), '-i', str(path),
               '-t', str(seconds), '-vn', '-ac', '1', '-ar', str(rate),
               '-f', 's16le', 'pipe:1']
    return np.frombuffer(subprocess.check_output(command), dtype='<i2').astype(np.float64)


def correlate(reference, observed):
    reference = np.asarray(reference, dtype=np.float64)
    observed = np.asarray(observed, dtype=np.float64).copy()
    if not len(observed) or len(reference) < len(observed):
        raise ValueError('invalid correlation windows')
    observed -= observed.mean()
    n = len(observed)
    size = 1 << (len(reference) + n - 2).bit_length()
    dot = np.fft.irfft(np.fft.rfft(reference, size) *
                       np.fft.rfft(observed[::-1], size), size)[n-1:len(reference)]
    sums = np.concatenate(([0.0], np.cumsum(reference)))
    squares = np.concatenate(([0.0], np.cumsum(reference * reference)))
    energy = squares[n:] - squares[:-n] - (sums[n:] - sums[:-n]) ** 2 / n
    denominator = np.sqrt(np.maximum(energy, 0) * np.dot(observed, observed))
    scores = np.divide(dot, denominator, out=np.zeros_like(dot), where=denominator > 0)
    index = int(np.argmax(np.abs(scores)))
    return index, float(scores[index])


def compare(source, capture, start, duration, search, rate, timeline_offset=None, tolerance=0.01):
    reference = decode(source, 0, search, rate)
    observed = decode(capture, start, duration, rate)
    if len(observed) < rate // 20 or len(reference) < len(observed):
        raise ValueError('audio window is too short')
    n = len(observed)
    lo, hi = 0, len(reference) - n
    if timeline_offset is not None:
        expected = start - timeline_offset
        lo = max(0, int((expected - tolerance) * rate))
        hi = min(hi, int((expected + tolerance) * rate))
        if lo > hi:
            raise ValueError('expected source window is outside the search range')
    index, score = correlate(reference[lo:hi+n], observed)
    index += lo
    result = dict(capture_start_seconds=start, capture_duration_seconds=n / rate,
                source_offset_seconds=index / rate, correlation=score,
                search_seconds=search, sample_rate=rate,
                scope='A high correlation supports a matching source window; weak scores are inconclusive and no score proves uninterrupted playback or mix quality.')
    if timeline_offset is not None:
        result.update(expected_source_seconds=start-timeline_offset,
                      expected_tolerance_seconds=tolerance)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--start', type=float, nargs='+', required=True,
                        help='one or more capture start times in seconds')
    parser.add_argument('--duration', type=float, default=1.0)
    parser.add_argument('--search', type=float, default=20.0)
    parser.add_argument('--rate', type=int, default=11025)
    parser.add_argument('--timeline-offset', type=float,
                        help='capture time minus source time; restrict matches to this shared timeline')
    parser.add_argument('--tolerance', type=float, default=0.01,
                        help='allowed source-time drift in seconds with --timeline-offset')
    args = parser.parse_args()
    if any(start < 0 for start in args.start) or args.duration <= 0 or args.search < args.duration or args.rate <= 0 or args.tolerance < 0:
        parser.error('invalid comparison window or sample rate')
    try:
        results = [compare(args.source, args.capture, start, args.duration, args.search, args.rate,
                           args.timeline_offset, args.tolerance)
                   for start in args.start]
        print(json.dumps(results[0] if len(results) == 1 else results, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
