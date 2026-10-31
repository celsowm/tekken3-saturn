#!/usr/bin/env python3
"""Compare image-submission intervals with particle-draw watch events."""
import argparse
import json
import re
import statistics
from pathlib import Path


def events(path):
    pattern = re.compile(r'^\s*frame (\d+) pc [0-9A-Fa-f]+ addr [0-9A-Fa-f]+ 32-bit value ([0-9A-Fa-f]+)')
    return [(int(m[1]), int(m[2], 16)) for line in path.read_text().splitlines()
            if (m := pattern.match(line))]


def summary(intervals, hz):
    if not intervals:
        raise ValueError('no intervals in selected range')
    fields = sum(intervals)
    return dict(images=len(intervals), fields=fields, fields_per_image=fields / len(intervals),
                median_fields=statistics.median(intervals), max_fields=max(intervals),
                submissions_per_second=hz * len(intervals) / fields, intervals=intervals)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--submissions', type=Path, required=True)
    p.add_argument('--particles', type=Path, required=True)
    p.add_argument('--hz', type=float, default=60)
    a = p.parse_args()
    submissions, particles = events(a.submissions), events(a.particles)
    if len(particles) < 2 or len(submissions) < 2:
        p.error('watch logs must contain at least two events each')
    start, end = particles[0][0], particles[-1][0]
    pairs = [(b[0], b[0] - a_[0]) for a_, b in zip(submissions, submissions[1:])
             if b[1] == a_[1] + 1]
    result = dict(particle_first_field=start, particle_last_field=end,
                  particle_event_fields=[x[0] for x in particles],
                  before=summary([dt for f, dt in pairs if f < start], a.hz),
                  during=summary([dt for f, dt in pairs if start < f <= end], a.hz),
                  after=summary([dt for f, dt in pairs if f > end], a.hz),
                  scope='Watch timestamps are emulator fields; submission cadence is not verified display swaps.')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
