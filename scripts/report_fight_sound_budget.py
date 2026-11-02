#!/usr/bin/env python3
"""Sound RAM a Tekken 3 Saturn fight keeps resident: shared effects plus both fighters' voices.

Reads the generated shared-effect table (build/generated/private_sfx/t3_local_sfx.h) and the fighter roster
(build/generated/fighters/roster.json). The game creates all of them with sat_sound_create at fight start, and
LibSaturn's pool aligns each to 2 bytes, so residency is the aligned sum. Profiles are LibSaturn's: 12 KiB driver
at the bottom and, unless streaming is dropped, a 64 KiB stream area at the top."""
import argparse
import itertools
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOUND_RAM = 0x80000
DRIVER = 0x3000
STREAM_AREA = 0x10000
SLOT_LIMIT = 64


def aligned(n):
    return (n + 1) & ~1


def shared(header):
    text = header.read_text()
    table = re.search(r'define T3_SFX_TABLE\s+(.*?)(?=\n#define|\Z)', text, re.S).group(1)
    rows = re.findall(r'\{\s*(\d+)u?,\s*(\d+)u?,\s*(\d+)u?\s*\}', table)
    return len(rows), sum(aligned(int(r[1])) for r in rows)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sfx-header', type=Path, default=ROOT / 'build/generated/private_sfx/t3_local_sfx.h')
    p.add_argument('--roster', type=Path, default=ROOT / 'build/generated/fighters/roster.json')
    p.add_argument('--pair', nargs=2, metavar='NAME', help='report one pair, e.g. PAUL LAW')
    args = p.parse_args()
    count, base = shared(args.sfx_header)
    fighters = [f for f in json.loads(args.roster.read_text()) if f['voices']]
    profiles = {'current': SOUND_RAM - DRIVER - STREAM_AREA, 'no_streaming': SOUND_RAM - DRIVER}

    def pair(a, b):
        voices = a['voices'] + (0 if a is b else b['voices'])     # a mirror match shares one set
        size = base + aligned(a['pcm_bytes']) + (0 if a is b else aligned(b['pcm_bytes']))
        return dict(pair=f"{a['name']} vs {b['name']}", sounds=count + voices, slots_of=SLOT_LIMIT, bytes=size,
                    **{f'free_{k}': v - size for k, v in profiles.items()})

    by_name = {f['name']: f for f in fighters}
    if args.pair:
        rows = [pair(by_name[args.pair[0]], by_name[args.pair[1]])]
    else:
        rows = sorted((pair(a, b) for a, b in itertools.combinations_with_replacement(fighters, 2)),
                      key=lambda r: r['bytes'])
        rows = [rows[0], rows[-1]]
    json.dump(dict(shared_sounds=count, shared_bytes=base, profile_bytes=profiles, pairs=rows), __import__('sys').stdout, indent=2)
    print()


if __name__ == '__main__':
    main()
