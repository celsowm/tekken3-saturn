#!/usr/bin/env python3
"""Is the sound continuous where one page hands over to the next?

Reads pages.bin for the stream time at which each page ends, then compares the capture with the model of what the
converter wrote (model_c352_scsp_render, all conversion effects on) in 200 ms windows centred on every hand-over
and in the same-sized windows elsewhere. A hand-over that dropped, repeated or delayed events would lower the
correlation there and not elsewhere."""
import argparse
import json
import struct
from pathlib import Path

import numpy as np

import c352_scsp_convert as convert
import model_c352_scsp_render as M


def page_ends(path):
    data, pos, ends = Path(path).read_bytes(), 0, []
    while pos < len(data):
        count, flags, base, end, _ = struct.unpack_from('>HHIII', data, pos)
        ends.append((base + end, bool(flags & 0x8000)))
        pos += 16 + 8 * count
    return ends


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vgm')
    parser.add_argument('--data', type=Path, required=True, help='converter output directory (pages.bin)')
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--lag', type=float, required=True)
    parser.add_argument('--start', type=float, required=True)
    parser.add_argument('--end', type=float, required=True)
    parser.add_argument('--tick-samples', type=int, default=256)
    parser.add_argument('--scale', type=float, default=1.0)
    parser.add_argument('--offset-ms', type=float, default=0.0, help='capture grid offset (attribute_c352_scsp_error.py)')
    args = parser.parse_args()
    info, rom, notes = M.load_notes(args.vgm, args.start, args.end, args.tick_samples)
    seconds = args.end - args.start
    model = M.render(notes, rom, seconds, ticks=True, fns=True, quant_gain=True, carry='scsp', scale=args.scale,
                     delta=args.offset_ms / 1000).sum(axis=1)
    raw = np.frombuffer(args.capture.read_bytes(), dtype='<i2').reshape(-1, 2).astype(float).sum(axis=1)
    cap = raw[int(args.lag * 44100):int(args.lag * 44100) + len(model)]
    half = int(0.1 * 44100)

    def corr(c):
        a, b = cap[c - half:c + half], model[c - half:c + half]
        return max(float(np.corrcoef(a, np.roll(model, -k)[c - half:c + half])[0, 1]) for k in range(-4, 5)) if np.std(a) > 1 else None

    ends = [t * convert.TICK_SECONDS for t, last in page_ends(args.data / 'pages.bin') if not last]
    at = [corr(int(t * 44100)) for t in ends]
    centres = [int(c * 44100) for c in np.arange(0.3, seconds - 0.3, 0.2)]
    away = [corr(c) for c in centres if all(abs(c / 44100 - t) > 0.25 for t in ends)]
    away = [a for a in away if a is not None]
    print(json.dumps(dict(handovers_s=ends, correlation_at_handover=at, mean_at_handover=float(np.mean([a for a in at if a is not None])),
                          correlation_elsewhere_mean=float(np.mean(away)), correlation_elsewhere_min=float(np.min(away)),
                          windows_elsewhere=len(away)), indent=2))


if __name__ == '__main__':
    main()
