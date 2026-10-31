#!/usr/bin/env python3
"""Inspect USA fighter or system sound IDs without writing converted game data."""
import argparse
import hashlib
import json
from pathlib import Path

import extract_psx_sfx as sfx


def inspect(exe, kind, requests):
    if hashlib.sha256(exe).hexdigest() != sfx.USA_EXE_SHA256:
        raise ValueError('unsupported USA executable')
    if kind == 'effect':
        resolve = sfx.effect_code
        header = exe[sfx.VH:sfx.VH + 0x1220]
        body = exe[sfx.VB:sfx.VB + sfx.VAB_BYTES - 0x1220]
    elif kind == 'system':
        resolve = sfx.system_code
        size = 0x820 + exe[sfx.SYS_VH + 0x12] * 0x200 + 0x200
        header = exe[sfx.SYS_VH:sfx.SYS_VH + size]
        body = exe[sfx.SYS_VB:sfx.SYS_VB + sfx.SYS_BYTES - size]
    else:
        raise ValueError('kind must be effect or system')
    rows = []
    for request in requests:
        code = resolve(exe, request)
        vag, rate, samples = sfx.vab_sound(header, body, code)
        rows.append(dict(request=f'0x{request:04X}', code=f'0x{code:04X}',
                         vag=vag, rate_hz=rate, samples=len(samples)))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True, help='user-supplied USA SLUS_004.02')
    parser.add_argument('--kind', choices=('effect', 'system'), required=True)
    parser.add_argument('ids', nargs='+', type=lambda value: int(value, 0),
                        help='effect requests such as 0x1053, or system IDs such as 0x3b')
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.exe.read_bytes(), args.kind, args.ids), indent=2))
    except (ValueError, OSError) as error:
        parser.exit(2, f'{error}\n')


if __name__ == '__main__':
    main()
