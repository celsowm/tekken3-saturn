#!/usr/bin/env python3
"""Size a compact SCSP sequence for a C352 VGM: one record group per sequencer frame (a 0x202 trigger).

Per frame: delta in frames (varint) and one op per touched voice: KEYON (voice, sample id, pitch, volumes),
VOL (voice, L, R), FREQ (voice, pitch), BOTH, KEYOFF (voice). Prints bytes raw and zlib-compressed, op counts, the
frame period and the quantisation error against a given timer tick (samples at 44,100 Hz)."""
import argparse
import collections
import json
import os
import statistics
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
import analyze_c352_vgm as A  # noqa: E402


def varint(n):
    out = bytearray()
    while True:
        b, n = n & 0x7F, n >> 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def encode(path):
    info = A.parse(A.read_vgm(path))
    frames, cur, ids, regs = [], [], {}, [[0] * 8 for _ in range(32)]
    touched = {}
    for t, reg, val in info['events']:
        if reg < 0x100:
            v, r = reg // 8, reg % 8
            regs[v][r] = val
            touched.setdefault(v, set()).add(r)
        elif reg == 0x202:
            frames.append((t, {v: (rs, list(regs[v])) for v, rs in touched.items()}))
            for v in touched:
                regs[v][3] &= ~(A.KEYON | A.KEYOFF)
            touched = {}
    stream, ops, last_frame, period = bytearray(), collections.Counter(), 0, 64    # delta unit: 64 samples = 1.45 ms
    for t, touched in frames:
        frame = round(t / period)
        stream += varint(frame - last_frame)
        last_frame = frame
        body = bytearray()
        for v, (rs, r) in sorted(touched.items()):
            if r[3] & A.KEYON:
                key = (r[4] & 255, r[5], r[6], r[3] & 0xB)
                ids.setdefault(key, len(ids))
                body += bytes([0x00 | v, ids[key]]) + r[2].to_bytes(2, 'big') + bytes([r[0] >> 8, r[0] & 255])
                ops['KEYON'] += 1
            elif r[3] & A.KEYOFF:
                body += bytes([0x40 | v]); ops['KEYOFF'] += 1
            elif 0 in rs and 2 in rs:
                body += bytes([0xC0 | v]) + r[2].to_bytes(2, 'big') + bytes([r[0] >> 8, r[0] & 255]); ops['BOTH'] += 1
            elif 0 in rs or 1 in rs:
                body += bytes([0x80 | v, r[0] >> 8, r[0] & 255]); ops['VOL'] += 1
            elif 2 in rs:
                body += bytes([0xA0 | v]) + r[2].to_bytes(2, 'big'); ops['FREQ'] += 1
        stream += bytes([len(touched)]) + body
    gaps = [b - a for (a, _), (b, _) in zip(frames, frames[1:])]
    return dict(frames=len(frames), ops=dict(ops), raw_bytes=len(stream), zlib_bytes=len(zlib.compress(bytes(stream), 9)),
                sample_table_entries=len(ids), delta_unit_samples=period, median_frame_gap_ms=statistics.median(gaps) / 44.1,
                frame_gap_ms_stdev=statistics.pstdev(gaps) / 44.1,
                duration_s=info['total'] / 44100), frames, period


def tick_error(frames, period, tick, fixed=True):
    """Error of scheduling each frame on a tick grid, accumulating the fractional period (no drift)."""
    errs = []
    for t, _ in frames:
        errs.append(abs(t - round(t / tick) * tick) / 44.1)
    return dict(tick_samples=tick, tick_ms=tick / 44.1, mean_ms=statistics.mean(errs), max_ms=max(errs))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('vgm')
    a = p.parse_args()
    report, frames, period = encode(a.vgm)
    report['tick_error'] = [tick_error(frames, 64, k) for k in (256, 128, 64, 32)]
    json.dump(report, sys.stdout, indent=2)
    print()


if __name__ == '__main__':
    main()
