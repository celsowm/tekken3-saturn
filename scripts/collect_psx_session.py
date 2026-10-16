#!/usr/bin/env python3
"""Collect a paused, read-only USA emulator capture and cross-check Lua/RAM.

Run after pcsx_redux_paul_session.lua pauses at the capture limit. This does
not start emulation, write guest RAM, or claim Saturn/original combat parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request

from analyze_psx_combat_oracle import read_capture, analyze
from psx_disc_import import US_EXE_SHA
from psx_ram_oracle import decode_snapshot, read_at, FIGHTER_BASE, FIGHTER_STRIDE


def collect(args):
    def fetch(endpoint):
        return urllib.request.urlopen(args.url.rstrip('/') + '/api/v1/' + endpoint,
                                      timeout=10).read()
    def digest(data):
        return hashlib.sha256(data).hexdigest()
    exe_hash = digest(args.exe.read_bytes())
    if exe_hash != US_EXE_SHA:
        raise ValueError('unsupported USA executable fingerprint')
    flow = json.loads(fetch('execution-flow'))
    if flow['running']:
        raise ValueError('emulator must be paused at the capture boundary')
    disc = json.loads(fetch('cd/info'))
    if disc['id'] != 'SLUS00402' or disc['label'].strip() != 'TEKKEN3':
        raise ValueError('mounted disc identity does not match Tekken 3 USA')
    ram, latest_bytes = fetch('cpu/ram/raw'), fetch('lua/t3-latest-combat')
    latest = [json.loads(line) for line in latest_bytes.splitlines()]
    if len(latest) != 2:
        raise ValueError('missing final paired Lua observation')
    source_frame, decoded = decode_snapshot(ram, latest[0]['tick'])
    if decoded != latest:
        raise ValueError('independent RAM decoder and paused Lua observation disagree')
    ticks = read_capture(args.trace)
    if not ticks or ticks[-1] != {r['fighter']: r for r in latest}:
        raise ValueError('trace does not end at the paused Lua/RAM checkpoint')
    ids = [read_at(ram, FIGHTER_BASE + i * FIGHTER_STRIDE + 0x18, 'H') for i in range(2)]
    if ids != [0, 0]:
        raise ValueError('paused encounter is not Paul against Paul')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    trace_data = args.trace.read_bytes()
    (out / 'trace.psx-combat.jsonl').write_bytes(trace_data)
    (out / 'paused-ram.bin').write_bytes(ram)
    (out / 'paused-lua.jsonl').write_bytes(latest_bytes)
    (out / 'screen.png').write_bytes(fetch('screen/still'))
    (out / 'events.json').write_text(json.dumps(analyze(ticks), indent=2) + '\n')
    manifest = dict(format=1, source='unmodified original USA game in PCSX-Redux',
                    exe_sha256=exe_hash, trace_sha256=digest(trace_data),
                    ram_sha256=digest(ram), character_ids=ids,
                    frames=len(ticks), first_source_frame=ticks[0][0]['source_frame'],
                    last_source_frame=source_frame, ram_lua_equal=True,
                    disc=disc, execution=flow, saturn_combat_parity=False,
                    capture_timing='GPU::Vsync observation; instruction-boundary alignment still pending')
    root = Path(__file__).resolve().parents[1]
    for name in ('pcsx_redux_capture_usa.lua', 'pcsx_redux_paul_session.lua'):
        manifest[name + '_sha256'] = digest((root / 'scripts' / name).read_bytes())
        shutil.copyfile(root / 'scripts' / name, out / name)
    if args.log:
        shutil.copyfile(args.log, out / 'emulator.log')
        manifest['log_sha256'] = digest(args.log.read_bytes())
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://localhost:8080')
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--log', type=Path)
    print(json.dumps(collect(parser.parse_args()), indent=2))
