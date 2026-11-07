#!/usr/bin/env python3
"""Compare the burst distribution of two effect-ring captures.

The ring (T3_EFFECT_RING) keeps the worst 32 images that showed hit
particles, each with the FRT of every phase of the effect path. The two
captures are separate emulator runs, so the game tick does not identify the
same image in both (the ring records the end-of-frame tick while the image
content is captured mid-frame); the comparison is therefore distributional:
the same statistic over each side's own worst 32 images, plus the run-level
counters from the observation.

The per-image equality of the two entry-list variants is proved in-build by
T3_MIXED_OT_LIVE_ENTRIES_CHECK, not here.
"""
import argparse
import json
from pathlib import Path

FIELDS = ('tick', 'image', 'particles', 'visible', 'faces', 'entries', 'mixed',
          'cover', 'sort', 'compose', 'prep', 'flush', 'commands', 'submitted',
          'ticks', 'projections', 'examined', 'depth', 'reserved0', 'reserved1')
FIELDS_PER_RECORD = 20
SLOTS = 32
FRT_PER_FIELD = 3728
FIELD_MS = 16.67


def ring_records(probe):
    probe = Path(probe)
    manifest = json.loads((probe / 'symbol-dumps.json').read_text())
    name = '_g_t3_effect_ring' if '_g_t3_effect_ring' in manifest else 'g_t3_effect_ring'
    raw = (probe / manifest[name]['file']).read_bytes()
    words = [int.from_bytes(raw[i:i + 4], 'big') for i in range(0, len(raw) - 3, 4)]
    records = []
    for slot in range(min(SLOTS, len(words) // FIELDS_PER_RECORD)):
        values = dict(zip(FIELDS, words[slot * FIELDS_PER_RECORD:(slot + 1) * FIELDS_PER_RECORD]))
        if values['image'] or values['visible']:
            values['total'] = values['mixed'] + values['prep'] + values['flush']
            records.append(values)
    records.sort(key=lambda r: r['image'])
    return records


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


def summary(records):
    if not records:
        return None
    totals = [r['total'] for r in records]
    to_ms = lambda value: round(value / FRT_PER_FIELD * FIELD_MS, 2)
    return dict(
        images=len(records),
        worst=to_ms(max(totals)),
        p95=to_ms(percentile(totals, 0.95)),
        median=to_ms(percentile(totals, 0.50)),
        mean=to_ms(sum(totals) / len(totals)),
        mixed=to_ms(sum(r['mixed'] for r in records)),
        cover=to_ms(sum(r['cover'] for r in records)),
        sort=to_ms(sum(r['sort'] for r in records)),
        compose=to_ms(sum(r['compose'] for r in records)),
        prep=to_ms(sum(r['prep'] for r in records)),
        flush=to_ms(sum(r['flush'] for r in records)),
        particles=sum(r['particles'] for r in records),
        visible=sum(r['visible'] for r in records),
        faces=sum(r['faces'] for r in records),
        entries=sum(r['entries'] for r in records),
        commands=sum(r['commands'] for r in records),
        submitted=sum(r['submitted'] for r in records),
        examined=sum(r['examined'] for r in records),
        depth=sum(r['depth'] for r in records))


def observation(probe):
    path = Path(probe) / 'observation.json'
    if not path.exists():
        return {}
    state = json.loads(path.read_text()).get('state', {})
    return {key: state.get(key) for key in (
        'sim_ticks', 'frame_serial', 'observed_fields', 'discarded_fields',
        'images_at_tick_mark', 'fields_at_tick_mark', 'psx_registered_hits',
        'psx_registered_blocks', 'psx_health_q16', 'first_error',
        'late_swaps', 'draw_waits', 'draw_timeouts', 'faces_rejected',
        'faces_culled', 'faces_submitted', 'commands_used', 'commands_capacity')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    args = parser.parse_args()
    sides = {}
    for name, path in (('control', args.control), ('candidate', args.candidate)):
        sides[name] = (summary(ring_records(path)), observation(path))
    control, candidate = sides['control'][0], sides['candidate'][0]
    if not control or not candidate:
        raise SystemExit('one side has no effect images')
    print(f"{'metric (ms)':<16}{'control':>12}{'candidate':>12}{'delta':>12}")
    for key in ('images', 'worst', 'p95', 'median', 'mean', 'mixed', 'cover', 'sort',
                'compose', 'prep', 'flush', 'particles', 'visible', 'faces', 'entries',
                'commands', 'submitted', 'examined', 'depth'):
        a, b = control[key], candidate[key]
        delta = round(b - a, 2) if isinstance(a, (int, float)) and isinstance(b, (int, float)) else ''
        print(f'{key:<16}{a:>12}{b:>12}{delta:>12}')
    print()
    for key, value in sides['control'][1].items():
        other = sides['candidate'][1].get(key)
        flag = '' if value == other else '  <-- differs'
        print(f'{key:<24}{str(value):<24}{str(other):<24}{flag}')


if __name__ == '__main__':
    main()
