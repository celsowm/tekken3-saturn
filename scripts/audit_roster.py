#!/usr/bin/env python3
"""Measure what each selectable fighter needs on the Saturn, from private records.

Reports, per character at costume 0: its costume slot, disc records, logic bank
bytes, compiled mesh counts, SKA1 archive bytes and texture bytes. Prints JSON;
reads private data and writes only to a scratch directory.

Costume keys and character records: tools/research/char_records.py and
docs/research/code/memory-map.md in the pinned Tekken3Remake. Required Notice:
Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0).
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from kmd_to_ska import compile_ska, parse_kmd

SCRIPTS = Path(__file__).resolve().parent
SLOTS = 52
FIRST_RECORD = 71
COMMON_RECORD = FIRST_RECORD + 4 * SLOTS
CHARACTERS = 21
KEYS_JP = 0x80095CE8
JP_BASE, JP_HEADER = 0x80010000, 0x800


def costume_keys(usa_exe, jp_exe):
    """The USA costume key table, found by its Japan Rev.1 bytes (identical rows)."""
    needle = jp_exe[KEYS_JP - JP_BASE + JP_HEADER:][:4 * CHARACTERS]
    at = usa_exe.find(needle)
    if at < 0 or usa_exe.find(needle, at + 1) >= 0:
        raise ValueError('costume key table not found exactly once in the USA executable')
    return struct.unpack_from(f'{4 * CHARACTERS}b', usa_exe, at)


def records(slot):
    kmd = FIRST_RECORD + 4 * slot
    return dict(kmd=kmd, vab=kmd + 1, arc=kmd + 2, bank=kmd + 3)


def logic_bank(raw_bank, scratch):
    subprocess.run([sys.executable, str(SCRIPTS / 'divmot_to_logic.py'), '--bank', str(raw_bank),
                    '--output', str(scratch), '--with-motion', '--with-hitboxes', '--with-events',
                    '--with-sounds'], check=True, stdout=subprocess.DEVNULL)
    found = sorted(scratch.glob('bank_*.t3logic'))
    if len(found) != 1:
        raise ValueError(f'expected one logic bank from {raw_bank}')
    return int(found[0].stem.split('_')[1]), found[0].stat().st_size


def measure(character, slot, raw, libsaturn, scratch):
    r = records(slot)
    work = scratch / f'{character:02}'
    work.mkdir()
    bank_type, bank_bytes = logic_bank(raw / f"{r['bank']:03}.bin", work)
    model = parse_kmd((raw / f"{r['kmd']:03}.bin").read_bytes())
    report = compile_ska(model, libsaturn, work / 'mesh', raw / f"{r['arc']:03}.bin")
    return dict(character=character, slot=slot, records=r, bank_type=bank_type, bank_bytes=bank_bytes,
                joints=report['joints'], vertices=report['compiled_vertices'],
                projected_vertices=report['logical_vertices'], faces=report['faces'],
                textures=report['textures'], scale=model.scale_percent,
                archive_bytes=(work / 'mesh/fighter_bind.ska').stat().st_size,
                arc_bytes=(raw / f"{r['arc']:03}.bin").stat().st_size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True, help='private directory of NNN.bin records')
    parser.add_argument('--usa-exe', type=Path, required=True)
    parser.add_argument('--jp-exe', type=Path, required=True)
    parser.add_argument('--libsaturn-source', type=Path, required=True)
    args = parser.parse_args()
    keys = costume_keys(args.usa_exe.read_bytes(), args.jp_exe.read_bytes())
    rows = []
    with tempfile.TemporaryDirectory() as scratch:
        for character in range(CHARACTERS):
            try:
                rows.append(measure(character, keys[4 * character], args.raw, args.libsaturn_source,
                                    Path(scratch)))
            except (ValueError, OSError, subprocess.CalledProcessError) as error:
                rows.append(dict(character=character, slot=keys[4 * character], error=str(error)))
    print(json.dumps(rows, indent=1))


if __name__ == '__main__':
    main()
