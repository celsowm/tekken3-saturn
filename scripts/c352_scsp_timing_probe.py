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


def generate_stress(out, per_tick, tick_samples, ticks, base=0x3000):
    """A paged stream with two audible clicks, `ticks - 16` ticks apart, and `per_tick` harmless register writes
    in every tick between them. If the 68000 cannot finish a tick's events inside one tick the interrupts merge, the
    driver tick falls behind the sample clock and the clicks come further apart than their tick count says."""
    out.mkdir(parents=True, exist_ok=True)
    tone = bytes(int(round(100 * math.exp(-i / 90) * math.sin(2 * math.pi * 2200 * i / 44100))) & 0xFF
                 for i in range(TONE_SAMPLES))
    ctrl = ((base >> 16) & 0xF) | convert.PCM8B
    events = [(2, 0x00, ctrl), (2, 0x02, base & 0xFFFF), (2, 0x04, 0), (2, 0x06, TONE_SAMPLES), (2, 0x0C, 0),
              (2, 0x16, 0xE000), (2, 0x10, 0), (4, 0x00, ctrl | convert.KYONB | convert.KYONEX),
              (7, 0x00, ctrl | convert.KYONEX)]
    last = ticks - 8
    events += [(last - 2, 0x00, ctrl), (last, 0x00, ctrl | convert.KYONB | convert.KYONEX), (last + 3, 0x00, ctrl | convert.KYONEX)]
    for t in range(10, last - 4):
        events += [(t, 31 * 0x20 + 0x12, 0)] * per_tick
    events.sort(key=lambda e: e[0])
    pages, i = [], 0
    while i < len(events):
        chunk = events[i:i + 512]
        j = i + len(chunk)
        pages.append((chunk, events[j][0] if j < len(events) else ticks, j >= len(events)))
        i = j
    blob = b''.join(struct.pack('>HHIII', len(c), 0x8000 if lastp else 0, 0, end, 0) + b''.join(struct.pack('>IHH', *e) for e in c)
                    for c, end, lastp in pages)
    (out / 'bank.bin').write_bytes(tone)
    (out / 'score.bin').write_bytes(b'')
    (out / 'pages.bin').write_bytes(blob)
    (out / 'score_meta.h').write_text(
        f"#define C352_BANK_BASE {base:#x}u\n#define C352_BANK_BYTES {len(tone)}u\n#define C352_SCORE_EVENTS 0u\n"
        f"#define C352_LOOP_TICKS {ticks}u\n#define C352_WINDOW_MS {round(ticks * tick_samples / 44.1)}u\n"
        f"#define C352_TICK_SAMPLES {tick_samples}u\n#define C352_PAGES {len(pages)}u\n#define C352_PAGED 1u\n"
        f"#define C352_LEAD_TICKS {round(0.3 * 44100 / tick_samples)}u\n")
    return dict(events=len(events), pages=len(pages), click_a_tick=4, click_b_tick=last, bytes=len(blob))


LOOP_SAMPLES = 441            # one 100 Hz cycle at 44100 Hz


def generate_keyoff(out, tick_samples=256, base=0x3000, notes=6):
    """A looped 100 Hz sine, keyed on and off `notes` times: the loop must come out at exactly 100 Hz (LEA exclusive)
    and the key-off tail shows how fast the SCSP's release (RR 31) is against the C352's immediate cut."""
    out.mkdir(parents=True, exist_ok=True)
    tone = b''.join(struct.pack('>b', int(round(100 * math.sin(2 * math.pi * i / LOOP_SAMPLES)))) for i in range(LOOP_SAMPLES)) + b'\0'
    ctrl = ((base >> 16) & 0xF) | convert.PCM8B | (1 << convert.LPCTL_SHIFT)
    per = round(0.6 * 44100 / tick_samples)
    on_len = round(0.3 * 44100 / tick_samples)
    events = []
    for k in range(notes):
        t = 10 + k * per
        events += [(t - 2, 0x00, ctrl), (t - 2, 0x02, base & 0xFFFF), (t - 2, 0x04, 0), (t - 2, 0x06, LOOP_SAMPLES),
                   (t - 2, 0x0C, 0), (t - 2, 0x16, 0xE000), (t - 2, 0x10, 0),
                   (t, 0x00, ctrl | convert.KYONB | convert.KYONEX), (t + on_len, 0x00, ctrl | convert.KYONEX)]
    events.sort(key=lambda e: e[0])
    end = 10 + notes * per + 20
    blob = struct.pack('>HHIII', len(events), 0x8000, 0, end, 0) + b''.join(struct.pack('>IHH', *e) for e in events)
    (out / 'bank.bin').write_bytes(tone)
    (out / 'score.bin').write_bytes(b'')
    (out / 'pages.bin').write_bytes(blob)
    (out / 'score_meta.h').write_text(
        f"#define C352_BANK_BASE {base:#x}u\n#define C352_BANK_BYTES {len(tone)}u\n#define C352_SCORE_EVENTS 0u\n"
        f"#define C352_LOOP_TICKS {end}u\n#define C352_WINDOW_MS {round(end * tick_samples / 44.1)}u\n"
        f"#define C352_TICK_SAMPLES {tick_samples}u\n#define C352_PAGES 1u\n#define C352_PAGED 1u\n"
        f"#define C352_LEAD_TICKS {round(0.3 * 44100 / tick_samples)}u\n")
    return dict(notes=notes, events=len(events), on_ticks=on_len)


def analyze_keyoff(capture, rate=44100):
    x = np.frombuffer(Path(capture).read_bytes(), dtype='<i2').reshape(-1, 2).astype(float).sum(axis=1)
    peak = np.max(np.abs(x))
    live = np.where(np.abs(x) > 0.05 * peak)[0]
    # split into notes at gaps over 100 ms
    groups, start = [], live[0]
    for a, b in zip(live[:-1], live[1:]):
        if b - a > int(0.1 * rate):
            groups.append((start, a))
            start = b
    groups.append((start, live[-1]))
    freqs, tails = [], []
    for a, b in groups:
        seg = x[a:b]
        core = seg[int(0.02 * rate):int(0.2 * rate)]
        cross = np.where((core[:-1] < 0) & (core[1:] >= 0))[0]
        frac = cross + (-core[cross]) / (core[cross + 1] - core[cross])
        freqs.append(float(rate * (len(frac) - 1) / (frac[-1] - frac[0])))
        # release: last sample above 50 %, 5 % and 1 % of the steady peak, in samples after the last 50 % crossing
        steady = np.max(np.abs(seg[int(0.02 * rate):int(0.2 * rate)]))
        env = np.abs(seg)
        idx = lambda frac_: int(np.where(env > frac_ * steady)[0][-1])
        tails.append(dict(to_50pct=idx(0.5), to_5pct=idx(0.05), to_1pct=idx(0.01)))
    return dict(notes=len(groups), loop_hz_mean=float(np.mean(freqs)), loop_hz_min=min(freqs), loop_hz_max=max(freqs),
                loop_hz_expected=100.0, loop_error_cents=float(1200 * math.log2(np.mean(freqs) / 100.0)),
                release_samples_after_last_50pct=[t['to_1pct'] - t['to_50pct'] for t in tails],
                release_1pct_minus_5pct_samples=[t['to_1pct'] - t['to_5pct'] for t in tails])


def analyze_stress(capture, click_a_tick, click_b_tick, tick_samples, rate=44100):
    x = np.frombuffer(Path(capture).read_bytes(), dtype='<i2').reshape(-1, 2).astype(float).sum(axis=1)
    thr = 0.12 * np.max(np.abs(x))
    onsets, i = [], 0
    while i < len(x):
        if abs(x[i]) > thr:
            onsets.append(i)
            i += int(0.1 * rate)
        else:
            i += 1
    if len(onsets) < 2:
        return dict(onsets=len(onsets))
    spacing = onsets[1] - onsets[0]
    expect = (click_b_tick - click_a_tick) * tick_samples
    return dict(onsets=len(onsets), spacing_samples=spacing, expected_samples=expect, lost_samples=spacing - expect,
                tick_rate_error_ppm=(spacing / expect - 1) * 1e6)


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
    st = sub.add_parser('stress-generate')
    st.add_argument('--out', type=Path, required=True)
    st.add_argument('--per-tick', type=int, required=True)
    st.add_argument('--tick-samples', type=int, default=256)
    st.add_argument('--ticks', type=int, default=240)
    sa = sub.add_parser('stress-analyze')
    sa.add_argument('--capture', type=Path, required=True)
    sa.add_argument('--ticks', type=int, default=240)
    sa.add_argument('--tick-samples', type=int, default=256)
    kg = sub.add_parser('keyoff-generate')
    kg.add_argument('--out', type=Path, required=True)
    ka = sub.add_parser('keyoff-analyze')
    ka.add_argument('--capture', type=Path, required=True)
    a = sub.add_parser('analyze')
    a.add_argument('--capture', type=Path, required=True)
    a.add_argument('--times', type=Path, required=True)
    args = parser.parse_args()
    if args.cmd == 'keyoff-generate':
        print(json.dumps(generate_keyoff(args.out)))
    elif args.cmd == 'keyoff-analyze':
        print(json.dumps(analyze_keyoff(args.capture), indent=2))
    elif args.cmd == 'stress-generate':
        print(json.dumps(generate_stress(args.out, args.per_tick, args.tick_samples, args.ticks)))
    elif args.cmd == 'stress-analyze':
        print(json.dumps(analyze_stress(args.capture, 4, args.ticks - 8, args.tick_samples)))
    elif args.cmd == 'generate':
        print(generate(args.out), 'events')
    else:
        print(json.dumps(analyze(args.capture, args.times), indent=2))


if __name__ == '__main__':
    main()
