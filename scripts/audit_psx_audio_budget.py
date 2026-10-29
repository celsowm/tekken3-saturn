#!/usr/bin/env python3
"""Measure complete private combat/system/fighter VABs without writing audio.

Uses the pinned remake's verified sample parser/decoder. Reports PCM payload
sizes with the original lead-in retained, before SCSP driver/allocator overhead.
No samples or converted game data are embedded in this source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
from decode_vab_sample import sample_bytes, decode_spu_adpcm
from extract_psx_sfx import arc_member, VH, VB, SYS_VH, SYS_VB
from extract_psx_sound_tables import HEADER


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/parity-reversal')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/parity-replay-audio-budget.json')
    args = parser.parse_args()
    cache = {}
    for line in (args.build / 'CMakeCache.txt').read_text().splitlines():
        if line and not line.startswith(('#', '//')) and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    imported = Path(cache['T3_LOCAL_SFX'])
    raw = (imported / 'SLUS_004.02').read_bytes()
    layout = json.loads((ROOT / 'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json').read_text())['blocks']['exe']
    if hashlib.sha256(raw[HEADER:]).hexdigest() != layout['sha256']:
        raise ValueError('USA executable body does not match pinned release layout')
    banks = []
    for name, h, b in [('combat', VH, VB), ('system', SYS_VH, SYS_VB)]:
        size = 0xA20 + struct.unpack_from('<H', raw, h + 0x12)[0] * 0x200
        total = struct.unpack_from('<I', raw, h + 12)[0]
        banks.append((name, raw[h:h + size], raw[b:b + total - size]))
    for name, vh, arc in [('actor0', imported / 'raw/072.bin', imported / 'raw/073.bin'),
                          ('actor1', imported / 'raw/080.bin', Path(cache['T3_LOCAL_ACTOR1_ARC']))]:
        banks.append((name, vh.read_bytes(), arc_member(arc.read_bytes(), 2)))
    seen, rows = {}, []
    for name, head, body in banks:
        if head[:4] != b'pBAV' or struct.unpack_from('<I', head, 12)[0] != len(head) + len(body):
            raise ValueError(f'invalid bank {name}')
        waves = struct.unpack_from('<H', head, 0x16)[0]
        sizes, loops = [], 0
        for vag in range(1, waves + 1):
            pcm, loop = decode_spu_adpcm(sample_bytes(head, body, vag))
            blob = struct.pack('<' + str(len(pcm)) + 'h', *pcm)
            seen[hashlib.sha256(blob).hexdigest()] = len(blob)
            sizes.append(len(blob))
            loops += loop is not None
        rows.append(dict(bank=name, vags=waves, adpcm_bytes=len(body),
                         pcm16_bytes=sum(sizes), pcm8_bytes=sum(sizes) // 2,
                         looping_samples=loops, source_sha256=hashlib.sha256(head + body).hexdigest()))
    report = dict(banks=rows, all_pcm16_bytes=sum(r['pcm16_bytes'] for r in rows),
                  unique_pcm16_bytes=sum(seen.values()), unique_pcm8_bytes=sum(seen.values()) // 2,
                  scsp_ram_bytes=524288, includes_original_lead_in=True,
                  scope='Complete four-bank payload only; not reachable-sound analysis or native playback proof')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
