#!/usr/bin/env python3
"""Print the per-effect-image costs recorded by the T3_EFFECT_RING probe.

The probe keeps the worst 32 images that showed hit particles, with the FRT
(1 field = 3728 ticks = 16.67 ms) of every phase of the effect path:

  mixed   the mixed fighter/particle OT (coverage test + sort + compose)
  cover   the per-face particle coverage test inside the mixed OT
  sort    the OT sort
  compose the ordered particle/mask composition (field 19: its face masks)
  prep    the particle preparation
  flush   the scene flush (VDP1 command submission)

The input is either a Mednafen fight report (scripts/mednafen_fight.py, whose
"symbols" section holds the ring as words) or the raw big-endian dump that
scripts/saturn_probe.py --dump-symbol writes.
"""
import argparse
import json
from pathlib import Path

FIELDS = ('tick', 'image', 'particles', 'visible', 'faces', 'entries', 'mixed',
          'cover', 'sort', 'compose', 'prep', 'flush', 'commands', 'submitted',
          'ticks', 'projections', 'examined', 'depth', 'coverp', 'composef')
FIELDS_PER_RECORD = 20
SLOTS = 32
FRT_PER_FIELD = 3728
FIELD_MS = 16.67


def ring_words(args):
    if args.report:
        report = json.loads(Path(args.report).read_text())
        symbols = report.get('symbols') or {}
        for name in ('_g_t3_effect_ring', 'g_t3_effect_ring'):
            if name in symbols:
                words = symbols[name]
                count = (symbols.get('_g_t3_effect_ring_count') or
                         symbols.get('g_t3_effect_ring_count') or [SLOTS])[0]
                return words[:count * FIELDS_PER_RECORD], count
        raise SystemExit('no effect ring in ' + args.report)
    raw = Path(args.bin).read_bytes()
    words = [int.from_bytes(raw[i:i + 4], 'big') for i in range(0, len(raw) - 3, 4)]
    count = min(SLOTS, len(words) // FIELDS_PER_RECORD)
    return words[:count * FIELDS_PER_RECORD], count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, help='Mednafen fight report with the ring in "symbols"')
    parser.add_argument('--bin', type=Path, help='raw big-endian ring dump')
    parser.add_argument('--images', type=int, default=0,
                        help='print only the N most expensive images (0: all)')
    args = parser.parse_args()
    if not args.report and not args.bin:
        parser.error('one of --report or --bin is required')
    words, count = ring_words(args)
    records = []
    for slot in range(count):
        record = words[slot * FIELDS_PER_RECORD:(slot + 1) * FIELDS_PER_RECORD]
        if len(record) < FIELDS_PER_RECORD:
            break
        if record[1] == 0 and record[3] == 0:
            continue
        records.append(record)
    records.sort(key=lambda r: r[6] + r[10] + r[11], reverse=True)
    if args.images:
        records = records[:args.images]
    header = (f"{'tick':>6} {'img':>5} {'part':>5} {'vis':>4} {'faces':>6} {'entries':>7} "
              f"{'mixed':>7} {'cover':>7} {'sort':>6} {'compose':>8} {'prep':>6} {'flush':>7} "
              f"{'cmds':>5} {'faces':>5} {'ticks':>5} {'proj':>4} {'exam':>6} {'depth':>6} {'ms':>6}")
    print(header)
    for record in records:
        values = dict(zip(FIELDS, record))
        total_ms = (values['mixed'] + values['prep'] + values['flush']) / FRT_PER_FIELD * FIELD_MS
        print(f"{values['tick']:>6} {values['image']:>5} {values['particles']:>5} {values['visible']:>4} "
              f"{values['faces']:>6} {values['entries']:>7} {values['mixed']:>7} {values['cover']:>7} "
              f"{values['sort']:>6} {values['compose']:>8} {values['prep']:>6} {values['flush']:>7} "
              f"{values['commands']:>5} {values['submitted']:>5} {values['ticks']:>5} "
              f"{values['projections']:>4} {values['examined']:>6} {values['depth']:>6} {total_ms:>6.2f}")
    if records:
        worst = max(records, key=lambda r: r[6] + r[10] + r[11])
        cover = sum(r[7] for r in records)
        sort = sum(r[8] for r in records)
        compose = sum(r[9] for r in records)
        mixed = sum(r[6] for r in records)
        prep = sum(r[10] for r in records)
        flush = sum(r[11] for r in records)
        print(f"\n{len(records)} effect images; worst mixed+prep+flush "
              f"{(worst[6] + worst[10] + worst[11]) / FRT_PER_FIELD * FIELD_MS:.2f} ms")
        coverp = sum(r[18] for r in records)
        composef = sum(r[19] for r in records)
        print(f"totals over the kept images (ms): mixed {mixed / FRT_PER_FIELD * FIELD_MS:.2f} "
              f"(cover {cover / FRT_PER_FIELD * FIELD_MS:.2f} of which particle bounds "
              f"{coverp / FRT_PER_FIELD * FIELD_MS:.2f}, sort {sort / FRT_PER_FIELD * FIELD_MS:.2f}, "
              f"compose {compose / FRT_PER_FIELD * FIELD_MS:.2f} of which face masks "
              f"{composef / FRT_PER_FIELD * FIELD_MS:.2f}), "
              f"prep {prep / FRT_PER_FIELD * FIELD_MS:.2f}, flush {flush / FRT_PER_FIELD * FIELD_MS:.2f}")


if __name__ == '__main__':
    main()
