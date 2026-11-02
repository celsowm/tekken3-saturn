#!/usr/bin/env python3
"""Render a converted window in Python, as the SCSP is meant to play it, and check a capture against it.

The model reads the same notes the converter does and plays each with linear interpolation at 44100 Hz. Two
switches add the conversion's own quantisation: --ticks puts every start, pitch change and end on the 5.805 ms
driver grid (plus an unknown start offset, scanned with --scan), --fns rounds the pitch to the SCSP's OCT/FNS.
With both on, a capture should match the model almost exactly; what is left between the capture and the C352
oracle is then the quantisation, not a mistake in the conversion. The model is not an SCSP emulator: it ignores
envelope shape, TL/DIPAN steps and the DAC."""
import argparse
import json
from pathlib import Path

import numpy as np

import analyze_c352_vgm as vgm
import c352_scsp_convert as convert


def scsp_rate(rate_hz):
    w = convert.pitch_word(rate_hz)
    octave = (w >> 11) & 0xF
    return 44100 * (1 + (w & 0x3FF) / 1024) * 2.0 ** (octave - 16 if octave & 8 else octave)


def render(notes, rom, seconds, ticks=False, fns=False, delta=0.0, out_rate=88200.0):
    n_out = int(seconds * 44100)
    q = (lambda t: convert.tick_of(t) * convert.TICK_SECONDS + delta) if ticks else (lambda t: t)
    mu = np.array(convert.mulaw_table(), dtype=float)
    out = np.zeros(n_out)
    for n in notes:
        base = (n.bank << 16) | n.start
        raw = np.frombuffer(bytes(rom[(base + i) % len(rom)] for i in range(n.length)), dtype=np.int8)
        data = mu[raw.astype(np.uint8)] if n.key[3] else raw.astype(float) * 256
        end = n.t_off if n.t_off is not None else n.t_natural
        i0, i1 = int(round(q(n.t_on) * 44100)), min(n_out, int(round(q(end) * 44100)))
        if i1 <= i0 or i0 < 0:
            continue
        ts = np.arange(i0, i1) / 44100
        steps = [(q(n.t_on), n.freq)] + [(q(c[0]), c[4]) for c in n.changes if c[1] == 'freq']
        rate = np.zeros(len(ts))
        for t_k, f_k in steps:
            r = out_rate * f_k / 65536
            rate[ts >= t_k] = scsp_rate(r) if fns and f_k else r
        pos = np.cumsum(rate / 44100)
        if n.looped:
            ls = (n.loop - n.start) & 0xFFFF
            pos = np.where(pos >= n.length, ls + ((pos - ls) % max(1, n.length - ls)), pos)
            ok = np.ones(len(pos), bool)
        else:
            ok = pos < n.length - 1
        p = np.where(ok, pos, 0)
        a = np.floor(p).astype(int)
        f = p - a
        v = data[a] * (1 - f) + data[np.minimum(a + 1, len(data) - 1)] * f
        gain = np.full(len(ts), sum(convert.gains(n.vol_f, n.vol_r)))
        for c in n.changes:
            if c[1] == 'vol':
                gain[ts >= q(c[0])] = sum(convert.gains(c[2], c[3]))
        out[i0:i1] += np.where(ok, v * gain, 0)
    return out


def block_corr(a, b, seconds, lag_search=4):
    out = []
    for s in range(int(seconds)):
        i = s * 44100
        x = a[i:i + 44100]
        out.append(max(float(np.corrcoef(x, np.roll(b, -k)[i:i + 44100])[0, 1]) for k in range(-lag_search, lag_search + 1)))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('vgm')
    parser.add_argument('--end', type=float, required=True)
    parser.add_argument('--start', type=float, default=0.0)
    parser.add_argument('--capture', type=Path, required=True, help='Ymir --dump-audio output')
    parser.add_argument('--lag', type=float, required=True, help='capture offset (s) from compare.json capture_lag_s')
    parser.add_argument('--scale', type=float, default=1.0, help='ignored by correlation; kept for gain comparisons')
    args = parser.parse_args()
    info = vgm.parse(vgm.read_vgm(args.vgm))
    rom = convert.rom_image(info)
    notes = [n for n in convert.timeline(info, args.start, args.end) if convert.audible(n)]
    seconds = args.end - args.start
    raw = np.frombuffer(args.capture.read_bytes(), dtype='<i2').reshape(-1, 2).astype(float).sum(axis=1)
    cap = raw[int(args.lag * 44100):int(args.lag * 44100) + int(seconds * 44100)]
    result = {}
    for name, kw in (('exact_times_exact_pitch', {}), ('exact_times_fns_pitch', dict(fns=True))):
        result[name] = block_corr(cap, render(notes, rom, seconds, **kw), seconds)
    scan = []
    for d in np.arange(-3.0, 3.01, 0.05):
        m = render(notes, rom, seconds, ticks=True, fns=True, delta=d / 1000)
        c = block_corr(cap, m, seconds)
        scan.append((float(np.mean(c)), float(d), c))
    best = max(scan)
    result['ticks_and_fns_pitch'] = dict(best_start_offset_ms=best[1], correlation_per_second=best[2], mean=best[0])
    for k in ('exact_times_exact_pitch', 'exact_times_fns_pitch'):
        result[k] = dict(correlation_per_second=result[k], mean=float(np.mean(result[k])))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
