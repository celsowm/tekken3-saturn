#!/usr/bin/env python3
"""Decode the original menu sounds (combat bank VAB 0 of the USA executable).

The title, mode list and select request these sound codes (Tekken3Remake
docs/research/code/modes.md; the code format is in code/sound.md): move the
select cursor 0x55F5, pick 0x50F4, mode list up/down 0x546C, mode confirm
0x4D87, START 0x4CC0. The decoder is scripts/extract_psx_sfx.py's. Output:
MENUSFX.BIN = 5 x (offset, samples, rate) as big-endian u32, padded to 64
bytes, then 8-bit signed PCM. Private: under assets/imported or build.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import extract_psx_sfx as sfx  # noqa: E402

CODES = (('move', 0x55F5), ('pick', 0x50F4), ('updown', 0x546C), ('confirm', 0x4D87), ('start', 0x4CC0))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents for root in (ROOT / 'assets/imported', ROOT / 'build')):
        p.error('output must be under assets/imported or build')
    d = args.exe.read_bytes()
    if hashlib.sha256(d).hexdigest() != sfx.USA_EXE_SHA256:
        p.error('needs the fingerprinted USA SLUS_004.02')
    vh = d[sfx.VH:sfx.VH + 0x1220]
    body = d[sfx.VB:sfx.VB + sfx.VAB_BYTES - 0x1220]
    table, pcm = bytearray(), bytearray()
    for name, code in CODES:
        vag, hz, samples = sfx.vab_sound(vh, body, code)
        while len(pcm) & 3:
            pcm.append(0)
        table += struct.pack('>3I', len(pcm), len(samples), hz)
        pcm += bytes((v >> 8) & 255 for v in samples)
        print(f'{name} {code:#06x} vag {vag} {hz} Hz {len(samples)} samples {len(samples) / hz:.2f} s')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(table).ljust(64, b'\0') + bytes(pcm))


if __name__ == '__main__':
    main()
