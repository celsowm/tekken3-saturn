#!/usr/bin/env python3
"""Report camera choice and compact stream sizes in a private raw DIVMOT bank."""
import argparse
import json
import struct
from pathlib import Path

from divmot_to_logic import sections_of
from psx_camera_tables import research


def inspect(path):
    data = path.read_bytes()
    _, sections = sections_of(data)
    choices = [struct.unpack_from('<hHi', data, at)
               for at in range(sections[7], sections[8] - 7, 8)]
    parser = research('camera_scripts')
    entries = []
    for _, _, script in choices:
        if script >= -1:
            continue
        offset = script & 0xffff
        if any(entry['offset'] == offset for entry in entries):
            continue
        address = sections[8] + offset
        mode, stream = parser.stream_at(data, address, None)
        entries.append(dict(offset=offset, yaw_mode=mode, frames=stream.frames,
                            encoded_bytes=2 + stream.size * 2))
    return dict(path=str(path), bank_type=data[1], choices=len(choices),
                streams=len(entries), encoded_bytes=sum(e['encoded_bytes'] for e in entries),
                sampled_bytes=sum(e['frames'] * 14 for e in entries), entries=entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('banks', type=Path, nargs='+')
    parser.add_argument('--summary', action='store_true', help='omit per-stream entries')
    args = parser.parse_args()
    try:
        report = [inspect(path) for path in args.banks]
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'{error}\n')
    if args.summary:
        for item in report:
            item.pop('entries')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
