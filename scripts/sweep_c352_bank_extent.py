#!/usr/bin/env python3
"""How much Sound RAM does the dynamic C352 bank need at once? Sweeps the planner's lead time and merge gap over
a VGM window and prints the packed extent, the live peak and the CD traffic of each setting (host only, no Ymir).

    python3 scripts/sweep_c352_bank_extent.py assets/imported/arcade-vgm/paul.vgz --end 131

The extent is the lowest budget `--bank-budget` accepts for that setting; the live peak is the lower bound no
placement can beat."""
import argparse
import itertools
import json

import c352_scsp_convert as convert


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('vgm')
    p.add_argument('--start', type=float, default=0.0)
    p.add_argument('--end', type=float, required=True)
    p.add_argument('--leads', type=float, nargs='+', default=[1.5, 1.0, 0.5, 0.25])
    p.add_argument('--gaps', type=float, nargs='+', default=[3.0, 1.0, 0.3, 0.0])
    p.add_argument('--tail', type=float, default=0.1)
    p.add_argument('--tick-samples', type=int, default=128)
    p.add_argument('--loop-passes', type=int, default=1)
    p.add_argument('--json', action='store_true')
    args = p.parse_args()
    r = convert.convert(args.vgm, args.start, args.end, 0x3000, 0.5, tick_samples=args.tick_samples,
                        loop_passes=args.loop_passes)
    rows = []
    for lead, gap in itertools.product(args.leads, args.gaps):
        _, _, rep = convert.plan_dynamic_bank(r['notes'], r['addresses'], 0x3000, 1 << 30, lead_s=lead,
                                              tail_s=args.tail, merge_gap_s=gap)
        rows.append(dict(lead_s=lead, merge_gap_s=gap, extent=rep['packed_extent_bytes'], peak_live=rep['peak_live_bytes'],
                         loads=rep['residencies'], bytes_loaded=rep['bytes_loaded']))
    if args.json:
        print(json.dumps(rows, indent=2))
        return
    print(f'{"lead":>5} {"gap":>5} {"extent":>9} {"live peak":>10} {"loads":>6} {"loaded":>10} {"CD B/s":>8}')
    span = args.end - args.start
    for x in rows:
        print(f'{x["lead_s"]:5.2f} {x["merge_gap_s"]:5.1f} {x["extent"]:9d} {x["peak_live"]:10d} {x["loads"]:6d} '
              f'{x["bytes_loaded"]:10d} {x["bytes_loaded"] / span:8.0f}')


if __name__ == '__main__':
    main()
