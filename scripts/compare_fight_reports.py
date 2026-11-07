#!/usr/bin/env python3
"""Compare two mednafen_fight.py reports over the tick marks both runs reached.

A slow host reaches fewer of the eight tick marks, so an aggregate marked rate
is only comparable over a shared span. This tool aligns the two runs on the mark
index (mark i is always simulation tick 600*(i+1)), recomputes the marked rate
over marks 1..N for both, and prints every shared segment plus the difference.

    python3 scripts/compare_fight_reports.py control/fight.json candidate/fight.json
"""
import argparse
import json
import sys
from pathlib import Path

NOISE = 0.04  # img/s, the documented run-to-run noise of the 280 s fight


def load(path):
    report = json.loads(Path(path).read_text())
    marks = report.get('marks') or []
    return report, marks


def rate(marks, count):
    (i0, f0), (i1, f1) = marks[0], marks[count]
    return round((i1 - i0) * 60 / (f1 - f0), 2)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('control', type=Path)
    ap.add_argument('candidate', type=Path)
    ap.add_argument('--noise', type=float, default=NOISE)
    args = ap.parse_args()
    control, cmarks = load(args.control)
    candidate, bmarks = load(args.candidate)
    if control.get('ai_pad_hash') != candidate.get('ai_pad_hash'):
        print(f"AI pad hash differs: {control.get('ai_pad_hash')} vs {candidate.get('ai_pad_hash')}")
        return 2
    if len(cmarks) < 2 or len(bmarks) < 2:
        print('need at least two reached marks in each report; rerun the harness (marks were added for this)')
        return 2
    shared = min(len(cmarks), len(bmarks))
    print(f"pad hash {control['ai_pad_hash']}; shared marks {shared} of {len(cmarks)}/{len(bmarks)}")
    print(f"{'segment':>7} {'control':>9} {'candidate':>10} {'delta':>7}")
    worst = None
    for n in range(1, shared):
        a, b = cmarks[n], bmarks[n]
        ca = (a[0] - cmarks[n - 1][0]) * 60 / (a[1] - cmarks[n - 1][1])
        cb = (b[0] - bmarks[n - 1][0]) * 60 / (b[1] - bmarks[n - 1][1])
        print(f"{n:>7} {ca:>9.2f} {cb:>10.2f} {cb - ca:>+7.2f}")
        if worst is None or cb - ca < worst[1]:
            worst = (n, cb - ca)
    ra, rb = rate(cmarks, shared - 1), rate(bmarks, shared - 1)
    print(f"marked rate over {shared - 1} spans: {ra:.2f} -> {rb:.2f} ({rb - ra:+.2f})")
    print(f"worst segment delta: segment {worst[0]} {worst[1]:+.2f} (noise +/-{args.noise})")
    verdict = 'inside noise' if abs(rb - ra) <= args.noise else ('candidate ahead' if rb > ra else 'candidate behind')
    print(f"verdict: {verdict}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
