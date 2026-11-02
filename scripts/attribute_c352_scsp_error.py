#!/usr/bin/env python3
"""Split the SCSP-against-C352 difference into its causes, one conversion effect at a time.

Stage models (model_c352_scsp_render.py) are added in order and each is compared with the oracle:
  A  C352 rules at 44.1 kHz (exact times, exact pitch, exact volumes)   -> resampling/interpolation only
  B  + pitch rounded to OCT/FNS                                          -> pitch
  C  + start/end/changes on the driver tick grid                         -> temporal quantisation
  D  + level and pan rounded to TL/DISDL/DIPAN, notes entering mid-sample -> gain and pan
and the capture is compared with D (what is left is the SCSP itself: interpolation, envelope, key-off, loop wrap)
and with the oracle. Levels are in the units of the run (--scale is applied to every model, the oracle already
carries libvgm's gain), so the level columns are the raw ones; the gain-matched view is a separate column."""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from scipy import signal

import compare_c352_scsp as K
import model_c352_scsp_render as M

STAGES = [('A_c352_rules_44k', {}), ('B_pitch_fns', dict(fns=True)), ('C_ticks', dict(fns=True, ticks=True)),
          ('D_gain_pan_carry', dict(fns=True, ticks=True, quant_gain=True, carry='scsp'))]


def metrics(ref, x):
    """ref and x: (n, 2) arrays at 44100 Hz, same start. Gain-matched where noted."""
    n = min(len(ref), len(x))
    ref, x = ref[:n], x[:n]
    rm, xm = ref.sum(axis=1), x.sum(axis=1)
    gain = K.rms(x) / max(K.rms(ref), 1e-9)
    wave = M.block_corr(rm, xm, n / 44100)
    xn = xm / max(gain, 1e-9)
    spec, _ = K.spectrogram_similarity(rm, xn)
    env_r, env_x = K.envelope(rm, 441), K.envelope(xn, 441)
    live = env_r > 100
    err = 20 * np.log10((env_x[live] + 1e-3) / (env_r[live] + 1e-3))
    pitch = K.pitch_summary(K.pitch_errors(rm, xn, 44100, 2.0))
    bands = [a - b for a, b in zip(K.band_levels(xm, 44100), K.band_levels(rm, 44100))]
    return dict(rms_ratio_db=K.db(K.rms(x), K.rms(ref)), waveform_corr_mean=float(np.mean(wave)),
                envelope_corr=float(np.corrcoef(env_x, env_r)[0, 1]),
                envelope_err_db_std_gain_matched=float(np.std(err)), spectrogram_cosine=spec,
                pitch_2s_median_cents=pitch['cents_median'], pitch_2s_mean_abs_cents=pitch['cents_mean_abs'],
                band_db_62_8k_max_abs=float(np.max(np.abs(bands[:7]))), band_db_8k_16k=bands[7], band_db_16k_22k=bands[8],
                lr_ratio_db=K.db(K.rms(x[:, 0]), K.rms(x[:, 1])), lr_ratio_db_oracle=K.db(K.rms(ref[:, 0]), K.rms(ref[:, 1])))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('vgm')
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--start', type=float, required=True)
    parser.add_argument('--end', type=float, required=True)
    parser.add_argument('--scale', type=float, default=1.0)
    parser.add_argument('--tick-samples', type=int, default=256)
    parser.add_argument('--lag', type=float, required=True, help='capture_lag_s from compare.json')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    info, rom, notes = M.load_notes(args.vgm, args.start, args.end, args.tick_samples)
    seconds = args.end - args.start
    n = int(seconds * 44100)
    ref88, _ = K.read_wav(args.oracle, seconds + 1.0, args.start)
    ref = signal.resample_poly(ref88, 1, 2, axis=0)[:n]
    raw = np.frombuffer(args.capture.read_bytes(), dtype='<i2').reshape(-1, 2).astype(float)
    cap = raw[int(args.lag * 44100):int(args.lag * 44100) + n]
    result = {}
    models = {}
    for name, kw in STAGES:
        models[name] = M.render(notes, rom, seconds, scale=args.scale, **kw)
        result[name + '_vs_oracle'] = metrics(ref, models[name])
    # the capture's own grid offset against the model grid: scan like model_c352_scsp_render.py
    best = max((np.mean(M.block_corr(cap.sum(axis=1), M.render(notes, rom, seconds, ticks=True, fns=True, quant_gain=True,
               carry='scsp', scale=args.scale, delta=d / 1000).sum(axis=1), seconds)), d)
               for d in np.arange(-3.0, 3.01, 0.1) * (args.tick_samples / 256))
    d_model = M.render(notes, rom, seconds, ticks=True, fns=True, quant_gain=True, carry='scsp', scale=args.scale,
                       delta=best[1] / 1000)
    result['capture_start_offset_ms'] = float(best[1])
    result['capture_vs_oracle'] = metrics(ref, cap)
    result['capture_vs_D_model_at_capture_grid'] = metrics(d_model, cap)
    text = json.dumps(result, indent=2)
    if args.out:
        args.out.write_text(text)
    print(text)


if __name__ == '__main__':
    main()
