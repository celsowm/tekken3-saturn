#!/usr/bin/env python3
"""Private glow-joint/hand-offset metadata; no converted data enters Git.

Addresses/rules from pinned Tekken3Remake tools/remake_import/tables.py and
core/effects/effect_objects.gd (PolyForm Noncommercial; NOTICE.md).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from extract_psx_ai_tables import window_bytes
from extract_psx_sound_tables import BASE, HEADER, ROOT


def normalize(raw, segments=None):
    def read(a, n):
        return window_bytes(raw, segments, a, n) if segments is not None else raw[a-BASE+HEADER:a-BASE+HEADER+n]
    return (b''.join(read(0x80025420+8*i, 4) for i in range(26)) + read(0x80025590, 21*16)
            # The breath, flame, cloud and gas offsets; burning joints; gas timetable; projectile frames.
            + read(0x80098BE0, 80) + read(0x800253DC, 40) + read(0x80025994, 56)
            + read(0x80025790, 8) + read(0x8002583C, 9) + read(0x800258E4, 21) + read(0x80025A44, 27))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,default=ROOT/'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--rev1',type=Path)
    p.add_argument('--out',type=Path,default=ROOT/'assets/imported/tekken3-usa/pose-tables/psx_effects.bin')
    a=p.parse_args();raw=a.exe.read_bytes()
    layout=json.loads((ROOT/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json').read_text())['blocks']['exe']
    if hashlib.sha256(raw[HEADER:]).hexdigest()!=layout['sha256']:raise ValueError('unrecognized USA executable')
    data=normalize(raw,layout['segments'])
    if len(data)!=681 or (a.rev1 and data!=normalize(a.rev1.read_bytes())):raise ValueError('effect table normalization mismatch')
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_bytes(data)
    print(json.dumps(dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),rev1_identical=bool(a.rev1))))

if __name__=='__main__':main()
