#!/usr/bin/env python3
"""Render a converted window in Python, as the chips are meant to play it, to attribute differences.

The model reads the same notes the converter does and plays each with linear interpolation at 44100 Hz, in
stereo. Switches add one conversion effect at a time:
  ticks      starts, pitch/volume changes and ends move to the driver tick grid (plus an unknown start offset)
  fns        pitch rounded to the SCSP's OCT/FNS word
  quant_gain level and pan rounded to TL (0.375 dB), DISDL and DIPAN steps, as the converter writes them
  carry      'exact' enters a note already sounding at the C352's sample position; 'scsp' as the converter does
With everything on, a capture should match the model closely; what is left between the model and the capture is
the SCSP itself (interpolation, envelope, key-off, loop wrap). The model is not an SCSP emulator."""
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


def scsp_gains(vf, vr, scale):
    """Left/right linear gain the SCSP applies for the TL/DISDL/DIPAN words the converter writes for this volume."""
    tl, pan = convert.level_pan(vf, vr, scale)
    disdl, dipan = pan >> 13, (pan >> 8) & 0x1F
    if disdl == 0 or tl == 0xFF and not pan:
        return 0.0, 0.0
    amp = 10 ** (-tl * 0.375 / 20) * 2.0 ** (disdl - 7)
    att = convert.pan_attenuation(dipan & 0xF)
    return (amp, amp * att) if dipan & 0x10 else (amp * att, amp)


def render(notes, rom, seconds, ticks=False, fns=False, quant_gain=False, carry='exact', scale=1.0, delta=0.0,
           out_rate=88200.0, tick_edges=True, tick_changes=True):
    n_out = int(seconds * 44100)
    t0 = convert.T0
    qt = (lambda t: convert.tick_of(t) * convert.TICK_SECONDS + delta) if ticks else (lambda t: t - t0)
    q = qt if tick_edges else (lambda t: t - t0)
    qc = qt if tick_changes else (lambda t: t - t0)
    mu = np.array(convert.mulaw_table(), dtype=float)
    out = np.zeros((n_out, 2))
    gain_of = (lambda vf, vr: scsp_gains(vf, vr, scale)) if quant_gain else \
              (lambda vf, vr: tuple(g * scale for g in convert.gains(vf, vr)))
    for n in notes:
        base = (n.bank << 16) | n.start
        raw = np.frombuffer(bytes(rom[(base + i) % len(rom)] for i in range(n.length)), dtype=np.int8)
        data = mu[raw.astype(np.uint8)] if n.key[3] else raw.astype(float) * 256
        end = n.t_off if n.t_off is not None else n.t_natural
        i0, i1 = int(round(q(n.t_on) * 44100)), min(n_out, int(round(q(end) * 44100)))
        if i1 <= i0 or i0 < 0:
            continue
        ts = np.arange(i0, i1) / 44100
        steps = [(q(n.t_on), n.freq)] + [(qc(c[0]), c[4]) for c in n.changes if c[1] == 'freq' and c[0] > n.t_on]
        rate = np.zeros(len(ts))
        for t_k, f_k in steps:
            r = out_rate * f_k / 65536
            rate[ts >= t_k] = scsp_rate(r) if fns and f_k else r
        ls = (n.loop - n.start) & 0xFFFF
        pos0 = float(int(getattr(n, 'consumed0', 0.0))) if n.carried else 0.0
        if n.carried and carry == 'scsp' and n.looped and pos0 >= ls:
            pos0 = float(ls)
        pos = pos0 + np.cumsum(rate / 44100)
        if n.looped:
            pos = np.where(pos >= n.length, ls + ((pos - ls) % max(1, n.length - ls)), pos)
            ok = np.ones(len(pos), bool)
        else:
            ok = pos < n.length - 1
        p = np.where(ok, pos, 0)
        a = np.floor(p).astype(int)
        f = p - a
        v = data[a] * (1 - f) + data[np.minimum(a + 1, len(data) - 1)] * f
        gl = np.full(len(ts), gain_of(n.vol_f, n.vol_r)[0])
        gr = np.full(len(ts), gain_of(n.vol_f, n.vol_r)[1])
        for c in n.changes:
            if c[1] == 'vol' and qc(c[0]) > q(n.t_on):
                gl[ts >= qc(c[0])], gr[ts >= qc(c[0])] = gain_of(c[2], c[3])
        out[i0:i1, 0] += np.where(ok, v * gl, 0)
        out[i0:i1, 1] += np.where(ok, v * gr, 0)
    return out


def block_corr(a, b, seconds, lag_search=4):
    out = []
    for s in range(int(seconds)):
        i = s * 44100
        x = a[i:i + 44100]
        out.append(max(float(np.corrcoef(x, np.roll(b, -k)[i:i + 44100])[0, 1]) for k in range(-lag_search, lag_search + 1)))
    return out


def load_notes(vgm_path, start, end, tick_samples=256):
    info = vgm.parse(vgm.read_vgm(vgm_path))
    convert.set_timebase(tick_samples, start)
    return info, convert.rom_image(info), [n for n in convert.timeline(info, start, end) if convert.audible(n)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('vgm')
    parser.add_argument('--end', type=float, required=True)
    parser.add_argument('--start', type=float, default=0.0)
    parser.add_argument('--tick-samples', type=int, default=256)
    parser.add_argument('--scale', type=float, default=1.0)
    parser.add_argument('--capture', type=Path, required=True, help='Ymir --dump-audio output')
    parser.add_argument('--lag', type=float, required=True, help='capture offset (s) from compare.json capture_lag_s')
    args = parser.parse_args()
    info, rom, notes = load_notes(args.vgm, args.start, args.end, args.tick_samples)
    seconds = args.end - args.start
    raw = np.frombuffer(args.capture.read_bytes(), dtype='<i2').reshape(-1, 2).astype(float).sum(axis=1)
    cap = raw[int(args.lag * 44100):int(args.lag * 44100) + int(seconds * 44100)]
    scan = []
    for d in np.arange(-3.0, 3.01, 0.05) * (args.tick_samples / 256):
        m = render(notes, rom, seconds, ticks=True, fns=True, quant_gain=True, carry='scsp', scale=args.scale,
                   delta=d / 1000).sum(axis=1)
        c = block_corr(cap, m, seconds)
        scan.append((float(np.mean(c)), float(d), c))
    best = max(scan)
    print(json.dumps(dict(best_start_offset_ms=best[1], mean=best[0], correlation_per_second=best[2]), indent=2))


if __name__ == '__main__':
    main()
