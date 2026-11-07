#!/usr/bin/env python3
"""Print the W1.1/W2.1 probe counters from a fight.json that carried --symbol.

    python3 scripts/report_ai_ring.py assets/imported/perf-w2-ai-280/fight.json

Reads `_g_t3_slave_frt` (8 words), `_g_t3_ai_ring` (8 events of 8 words),
`_g_t3_ai_ring_events`, `_g_t3_ai_ring_over_half` and `_g_t3_ai_ring_over_field`.
Sizes come from the ELF symbol table, so the dumper may pad the word lists; the
script only reads the fixed prefixes above.
"""
import argparse
import json
import sys
from pathlib import Path

FRT_PER_FIELD = 3728


def words(report, name):
    value = report.get('symbols', {}).get(name)
    if value is None:
        raise SystemExit(f'{name} is not in the report; run mednafen_fight.py with --symbol {name}')
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('report', type=Path)
    ap.add_argument('--elf', type=Path, help='optional: read the same symbols from a probe symbol-dump JSON')
    args = ap.parse_args()
    report = json.loads(args.report.read_text())
    if '_g_t3_slave_frt' in report.get('symbols', {}):
        f = words(report, '_g_t3_slave_frt')
        names = ['ticks', 'actor preparation', 'stage tables', 'stage (Slave)', 'stage (Master)',
                 'purge FRT', 'purge calls', 'purge worst']
        print('Slave FRT totals (1 field = %d ticks):' % FRT_PER_FIELD)
        for i, name in enumerate(names):
            value = f[i]
            print(f'  {i}: {name:<18} {value:>10}  ({value / FRT_PER_FIELD:.3f} fields'
                   + (f', {value / max(f[6], 1):.0f} ticks/call' if i == 5 else '') + ')')
    if '_g_t3_ai_ring_events' in report.get('symbols', {}):
        ring = words(report, '_g_t3_ai_ring')
        events = words(report, '_g_t3_ai_ring_events')[0]
        half = words(report, '_g_t3_ai_ring_over_half')[0]
        over = words(report, '_g_t3_ai_ring_over_field')[0]
        print(f'\nAiUpdate calls {events}: {half} over half a field, {over} over a full field')
        print(f"{'#':>2} {'FRT':>7} {'tick':>6} {'fighter':>7} {'slot':>5} {'window':>6} {'picks':>6}"
              f" {'reads':>6} {'cond':>6} {'miss':>5} {'held':>6} {'pressed':>7}")
        for i in range(8):
            e = ring[i * 8:i * 8 + 8]
            if not e[0]:
                continue
            # Big-endian halves: the first field of each word pair is the high half.
            fighter, slot = e[5] >> 16, e[5] & 0xFFFF
            window, picks = e[6] >> 16, e[6] & 0xFFFF
            held, pressed = e[7] >> 16, e[7] & 0xFFFF
            print(f"{i:>2} {e[0]:>7} {e[1]:>6} {fighter:>7} {slot - 256 if slot > 127 else slot:>5}"
                  f" {window:>6} {picks:>6} {e[2]:>6} {e[3]:>6} {e[4]:>5} {held:>6} {pressed:>7}")
        print('slot -1 means no move row; window = active bus windows; picks = pick-cache cursor;'
              ' reads/cond/miss are deltas since the previous update of the same fighter.')
    if '_g_stage_owner_claims' in report.get('symbols', {}):
        claims = words(report, '_g_stage_owner_claims')
        ftr = words(report, '_g_stage_owner_ftr')
        worst = words(report, '_g_stage_owner_worst')
        print('\nStage tables by deferred-tick bucket (FRT /128; 1 field = %d):' % FRT_PER_FIELD)
        print(f"{'ticks':>5} {'Master':>7} {'Slave':>6} {'M ms/call':>10} {'S ms/call':>10}")
        for b in range(7):
            mc, sc = claims[b], claims[7 + b]
            if not mc and not sc:
                continue
            mm = ftr[b] / mc / FRT_PER_FIELD * 16.67 if mc else 0.0
            sm = ftr[7 + b] / sc / FRT_PER_FIELD * 16.67 if sc else 0.0
            print(f'{b:>5} {mc:>7} {sc:>6} {mm:>10.2f} {sm:>10.2f}')
        print(f'worst: Master {worst[0]} ticks, Slave {worst[1]} ticks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
