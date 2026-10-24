#!/usr/bin/env python3
"""Inspect guest diagnostics even when a Ymir probe stopped before observation.json.

    python3 scripts/inspect_saturn_dump.py assets/imported/t30-law-ai-round \
      --fields sim_ticks psx_round_state psx_health_q16
"""
import argparse
import json
from pathlib import Path

from saturn_probe import decode, decode_panic


def inspect(directory, fields, symbols):
    state = decode((directory / 'diagnostics.bin').read_bytes())
    missing = [key for key in fields if key not in state]
    if missing:
        raise ValueError(f'{directory}: unknown state fields: {missing}')
    panic_path = directory / 'native-panic.bin'
    panic = decode_panic(panic_path.read_bytes()) if panic_path.exists() else None
    result = {'path': str(directory), 'panic': panic,
              'state': {key: state[key] for key in fields} if fields else state}
    observation = directory / 'observation.json'
    if observation.exists():
        saved = json.loads(observation.read_text())
        result['provenance'] = saved.get('provenance')
    if symbols:
        manifest = json.loads((directory / 'symbol-dumps.json').read_text())
        result['symbols'] = {}
        for requested in symbols:
            name = requested if requested in manifest else '_' + requested
            if name not in manifest:
                raise ValueError(f'{directory}: missing dumped symbol {requested}')
            entry = manifest[name]
            raw = (directory / entry['file']).read_bytes()
            if len(raw) != entry['bytes'] or len(raw) % 4 or len(raw) > 64:
                raise ValueError(f'{directory}: {requested} is not a short 32-bit array')
            result['symbols'][requested] = [int.from_bytes(raw[i:i+4], 'big', signed=True)
                                             for i in range(0, len(raw), 4)]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', type=Path, nargs='+')
    parser.add_argument('--fields', nargs='+', default=[])
    parser.add_argument('--symbols', nargs='+', default=[],
                        help='decode dumped 32-bit diagnostic symbols')
    args = parser.parse_args()
    try:
        results = [inspect(path, args.fields, args.symbols) for path in args.directories]
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
