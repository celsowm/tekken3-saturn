#!/usr/bin/env python3
"""Measure what the 68000 score does to timing, with no music in the way.

`generate` writes a bank (one short decaying tone) and a score of isolated notes at deliberately off-grid times
(every phase of the 5.805 ms tick is covered) in the same files the converter writes, so the prototype program
plays them unchanged. `analyze` finds each note's onset in the Ymir capture and compares it with the requested
time and with the tick-quantised time: the quantisation error, the jitter around the tick grid, and the clock
drift between the driver ticks and the requested timeline."""
import argparse
import json
import math
import struct
from pathlib import Path

import numpy as np

import c352_scsp_convert as convert

NOTES = 64
SPACING = 0.150
TONE_SAMPLES = 400            # 9 ms at 44100 Hz: a 2.2 kHz sine with a fast decay


def note_times():
    # off-grid on purpose: a drifting fraction of the tick, plus a 7 ppm-sized detune that would show as drift
    return [0.2 + k * SPACING + (k * 0.00371 % convert.TICK_SECONDS) for k in range(NOTES)]


def generate(out, base=0x3000):
    out.mkdir(parents=True, exist_ok=True)
    tone = bytes(int(round(100 * math.exp(-i / 90) * math.sin(2 * math.pi * 2200 * i / 44100))) & 0xFF
                 for i in range(TONE_SAMPLES))
    events, times = [], note_times()
    for k, t in enumerate(times):
        slot, tick = k % 4, convert.tick_of(t)
        b = slot * 0x20
        ctrl = ((base >> 16) & 0xF) | convert.PCM8B
        events += [(tick - 2, b + 0x00, ctrl), (tick - 2, b + 0x02, base & 0xFFFF), (tick - 2, b + 0x04, 0),
                   (tick - 2, b + 0x06, TONE_SAMPLES), (tick - 2, b + 0x0C, 0), (tick - 2, b + 0x16, 0xE000),
                   (tick - 2, b + 0x10, 0),
                   (tick, b + 0x00, ctrl | convert.KYONB | convert.KYONEX),
                   (tick + 3, b + 0x00, ctrl | convert.KYONEX)]
    events.sort(key=lambda e: e[0])
    loop_ticks = convert.tick_of(times[-1]) + 40
    (out / 'bank.bin').write_bytes(tone)
    (out / 'score.bin').write_bytes(b''.join(struct.pack('>IHH', *e) for e in events))
    (out / 'score_meta.h').write_text(
        f"#define C352_BANK_BASE {base:#x}u\n#define C352_BANK_BYTES {len(tone)}u\n#define C352_SCORE_EVENTS {len(events)}u\n"
        f"#define C352_LOOP_TICKS {loop_ticks}u\n#define C352_WINDOW_MS {round(times[-1] * 1000)}u\n")
    (out / 'times.json').write_text(json.dumps(dict(times=times, ticks=[convert.tick_of(t) for t in times],
                                                    tick_seconds=convert.TICK_SECONDS)))
    return len(events)


def analyze(capture, times_json, rate=44100):
    meta = json.loads(Path(times_json).read_text())
    times, ticks, ts = meta['times'], meta['ticks'], meta['tick_seconds']
    x = np.frombuffer(Path(capture).read_bytes(), dtype='<i2').reshape(-1, 2).astype(float).sum(axis=1)
    peak = np.max(np.abs(x))
    thr = 0.12 * peak
    onsets, i = [], 0
    while i < len(x):
        if abs(x[i]) > thr:
            onsets.append(i / rate)
            i += int(0.1 * rate)
        else:
            i += 1
    onsets = onsets[:NOTES]
    n = min(len(onsets), NOTES)
    on, req, tick_t = np.array(onsets[:n]), np.array(times[:n]), np.array(ticks[:n]) * ts
    # fit capture = a + b * tick time: a is the program/driver start offset, b - 1 the clock drift
    b, a = np.polyfit(tick_t, on, 1)
    resid = on - (a + b * tick_t)
    quant = tick_t - req
    total = on - (on[0] - req[0]) - req
    return dict(
        notes_requested=NOTES, onsets_found=len(onsets),
        tick_grid=dict(clock_ratio=float(b), drift_ppm=float((b - 1) * 1e6), drift_over_10s_ms=float((b - 1) * 10000),
                       jitter_ms_std=float(resid.std() * 1000), jitter_ms_max_abs=float(np.max(np.abs(resid)) * 1000),
                       note='onset detection is sample-quantised: 1 sample = 0.0227 ms'),
        quantisation=dict(requested_to_tick_ms_mean_abs=float(np.mean(np.abs(quant)) * 1000),
                          requested_to_tick_ms_max_abs=float(np.max(np.abs(quant)) * 1000)),
        end_to_end=dict(error_ms_mean=float(total.mean() * 1000), error_ms_std=float(total.std() * 1000),
                        error_ms_max_abs=float(np.max(np.abs(total - total.mean())) * 1000),
                        note='capture onset minus requested time, after removing the constant start offset'),
        start_offset_s=float(a))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='cmd', required=True)
    g = sub.add_parser('generate')
    g.add_argument('--out', type=Path, required=True)
    a = sub.add_parser('analyze')
    a.add_argument('--capture', type=Path, required=True)
    a.add_argument('--times', type=Path, required=True)
    args = parser.parse_args()
    if args.cmd == 'generate':
        print(generate(args.out), 'events')
    else:
        print(json.dumps(analyze(args.capture, args.times), indent=2))


if __name__ == '__main__':
    main()
