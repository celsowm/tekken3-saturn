#!/usr/bin/env python3
"""Export a recorded Tekken Force game (`scripts/force_flow_record.py`) as the three-fighter comparator's input.

Writes, under --out: `seed.bin` (a 2 MiB USA-layout RAM image of the state after the seed frame: the
three fighter records and every recorded range mapped from Rev.1), `frame-N.ranges` (the same spans
after each later running Force frame, the 'T3RANGES' format of fight_oracle_export.py), `cases.tsv`
(the evaluator's row: path, four zero mapped pads, call, frame, the physical pads, the edges) and `seed.json`. The seed is the last frame before the
first running one whose three records all stand neutral (move slot 3, no pending move). Nothing here is
game data a repository may keep; the output stays under build/. Run with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/force_oracle_export.py \
        --trace third_party/Tekken3Remake/work/traces/flow_force_run.bin --out build/force-oracle/run
"""
import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import check_force_flow_trace as cf        # noqa: E402
import fight_oracle_export as ox           # noqa: E402


def neutral(record):
    f = ox.neutral_facts(record)
    return (f['slot'] == 3 and not f['pending'] and f['row'] and not f['w404'] and not f['w406']
            and f['blend_row'] and not f['blend_mode'])


def usa_spans(layout, header, ranges, records):
    """[(USA address, bytes)] of one frame: the recorded ranges and the three fighter records."""
    out = []
    for (name, address), data in ranges.items():
        try:
            out.append((layout.to_usa(address), data))
        except (ValueError, IndexError):
            continue   # outside the matched stretches (overlay data): not a USA address
    for i, record in enumerate(records):
        out.append((ox.USA_FIGHTERS + i * ox.FIGHTER_SIZE, record))
    return clip(out)


def clip(spans):
    """Only the bytes the evaluator reads (fight_oracle_export.GLOBALS and the fighter records), in at most
    64 spans: the snapshot reader's limit."""
    wanted = sorted(ox.GLOBALS) + [(ox.USA_FIGHTERS + i * ox.FIGHTER_SIZE, ox.FIGHTER_SIZE) for i in range(3)]
    out = {}
    for address, data in spans:
        for w_address, w_size in wanted:
            lo, hi = max(address, w_address), min(address + len(data), w_address + w_size)
            if lo < hi:
                out[lo] = data[lo - address:hi - address]
    return sorted(out.items())


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--ticks', type=int, default=3000)
    parser.add_argument('--skip', type=int, default=0, help='seed no earlier than this frame')
    parser.add_argument('--seed-at', type=int, help='seed after exactly this frame, neutral or not (a start-of-level run: the owned match initializes itself)')
    args = parser.parse_args()
    layout = ox.Layout(ox.LAYOUT)
    header, body = cf.load_trace(args.trace)
    args.out.mkdir(parents=True, exist_ok=True)
    seeded = None
    rows = []
    previous = None
    last_pad = 0
    for k, (pads, ranges, records) in enumerate(cf.frames(header, body)):
        running = len(records) == 3 and cf.build_ram(header, ranges, records).s32(0x800AFF50) == 8
        if seeded is None:
            if running and previous is not None and k > args.skip and (k == args.seed_at if args.seed_at is not None else all(neutral(r) for r in records)):
                ram = bytearray(0x200000)
                for address, data in usa_spans(layout, header, ranges, records):
                    ram[address - 0x80000000:address - 0x80000000 + len(data)] = data
                (args.out / 'seed.bin').write_bytes(ram)
                seeded = k
            previous = (ranges, records) if running else None
            continue
        if len(rows) >= args.ticks or not running:
            break
        path = args.out / f'frame-{k}.ranges'
        chunks = usa_spans(layout, header, ranges, records)
        path.write_bytes(b'T3RANGES' + struct.pack('<I', len(chunks))
                         + b''.join(struct.pack('<II', a, len(d)) + d for a, d in chunks))
        # the evaluator's row: path, mapped pads (not known here), call, frame, physical pads, edges
        edge = pads[0] & ~last_pad & 0xFFFF
        rows.append(f'{path}\t0\t0\t0\t0\t{k}\t{k}\t{pads[0]}\t{pads[1]}\t{edge}\t0\n')
        last_pad = pads[0]
    if seeded is None:
        raise SystemExit('no neutral running Force frame to seed from')
    (args.out / 'cases.tsv').write_text(''.join(rows))
    (args.out / 'seed.json').write_text(json.dumps(dict(trace=str(args.trace), seed_frame=seeded, ticks=len(rows)), indent=2) + '\n')
    print(f'{args.out}: seed after frame {seeded}, {len(rows)} ticks')


if __name__ == '__main__':
    main()
