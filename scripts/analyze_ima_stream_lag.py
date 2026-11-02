#!/usr/bin/env python3
"""Where a captured stream falls behind the oracle, block by block, with a search window wider than compare's.

compare_c352_scsp.py tracks the lag in 1 s blocks within +-8 ms; a stream that steps by a whole ADPCM block
(23 ms) or by a segment falls outside that window and reads as a loss of correlation. This script finds the lag of
every block inside +-W ms of the global alignment and reports where the lag changes, so a step is visible as a
step.

    python3 scripts/analyze_ima_stream_lag.py --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav \\
        --capture assets/imported/ima-proto/run/audio.s16le --seconds 206 --out assets/imported/ima-proto/run/lag.json

Writes lag.json (under --out) and prints a summary. The capture is 44100 Hz stereo s16le, as compare expects."""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import signal

import compare_c352_scsp as compare

RATE = compare.RATE


def first_sound(x):
    return int(np.argmax(np.abs(x) > 0.05 * np.max(np.abs(x[:RATE]))))


def fractional_warp(cap, glob, intercept_ms, slope_ms, n_out, half=16):
    """Output frame n is the capture at n + glob + lag(n), lag(n) = intercept + slope * t in samples, read with a
    Kaiser-windowed sinc of 2 * half taps. Returns (n_out, 2) float."""
    out = np.zeros((n_out, 2))
    n = np.arange(n_out, dtype=np.float64)
    pos = n + glob + (intercept_ms + slope_ms * n / RATE) * RATE / 1000.0
    base = np.floor(pos).astype(np.int64)
    frac = pos - base
    front = max(0, half - int(base.min()))                                # zeros before the start, if needed
    padded = np.zeros((front + max(len(cap), int(base.max()) + 1) + 2 * half + 2, 2))
    padded[front + half:front + half + len(cap)] = cap                    # zeros past the end as well
    for j in range(-half + 1, half + 1):
        x = j - frac
        taps = np.sinc(x) * (0.5 + 0.5 * np.cos(np.pi * x / half))
        out += padded[front + base + j + half] * taps[:, None]
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--seconds', type=float, required=True)
    parser.add_argument('--block', type=float, default=0.5, help='seconds per measured block')
    parser.add_argument('--window-ms', type=float, default=60.0, help='search half-width around the global lag')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--write-aligned', type=Path, help='also write the capture moved onto the oracle timeline (s16le)')
    parser.add_argument('--expect', type=Path, help='s16le the capture should equal (the software decode): per-second SNR')
    parser.add_argument('--warp-check', action='store_true', help='with --expect: validate the warp on the expected audio')
    args = parser.parse_args()

    ref88, rate = compare.read_wav(args.oracle, args.seconds + 1.0)
    ref = signal.resample_poly(ref88, 1, rate // RATE, axis=0)
    cap = np.frombuffer(args.capture.read_bytes(), dtype='<i2').reshape(-1, 2).astype(np.float64)
    ref_mono, cap_mono = ref.sum(axis=1), cap.sum(axis=1)
    n_ref = int(args.seconds * RATE)

    coarse = first_sound(cap_mono) - first_sound(ref_mono)
    w = int(args.window_ms / 1000 * RATE)
    glob, glob_corr = compare.best_lag(ref_mono[:n_ref], cap_mono, max(0, coarse - w), coarse + w)

    block = int(args.block * RATE)
    rows = []
    for s in range(0, n_ref - block, block):
        a = ref_mono[s:s + block]
        if compare.rms(a) < 150:
            continue
        lo = max(0, glob + s - w)
        hi = glob + s + w
        lag, corr = compare.best_lag(a, cap_mono[:glob + s + block + 2 * w], lo, hi)
        rows.append(dict(t=round(s / RATE, 3), s=s, lag_samples=int(lag - glob - s),
                         lag_ms=round((lag - glob - s) / RATE * 1000, 3), corr=round(corr, 4)))

    lags = np.array([r['lag_ms'] for r in rows])
    steps = np.diff(lags) if len(lags) > 1 else np.zeros(0)
    at_bound = int(np.sum(np.abs(lags) >= args.window_ms - 0.5))
    summary = dict(
        capture=str(args.capture), seconds=args.seconds, global_lag_samples=int(glob),
        global_corr=round(glob_corr, 4), block_s=args.block, window_ms=args.window_ms, blocks=len(rows),
        lag_ms_min=float(lags.min()) if len(lags) else None, lag_ms_max=float(lags.max()) if len(lags) else None,
        lag_ms_std=float(lags.std()) if len(lags) else None,
        blocks_at_window_edge=at_bound,
        corr_below_0_5=int(sum(r['corr'] < 0.5 for r in rows)),
        corr_min=min((r['corr'] for r in rows), default=None),
        largest_step_ms=float(np.max(np.abs(steps))) if len(steps) else 0.0,
        steps_over_2ms=int(np.sum(np.abs(steps) > 2.0)),
        rows=rows)
    if args.write_aligned or args.expect:
        # The drift is a straight line in the block lags (a constant clock ratio), so the capture is moved by that
        # line with a fractional delay: a whole-sample shift per block would leave up to half a sample of error.
        t_rows = np.array([r['t'] for r in rows])
        slope_ms, intercept_ms = np.polyfit(t_rows, [r['lag_ms'] for r in rows], 1)
        aligned = fractional_warp(cap, glob, intercept_ms, slope_ms, n_ref)
        aligned = aligned.clip(-32768, 32767).astype('<i2').astype(np.float64)
        summary['drift_fit'] = dict(slope_ms_per_s=float(slope_ms), intercept_ms=float(intercept_ms))
    else:
        slope_ms = intercept_ms = 0.0
    if args.write_aligned:
        args.write_aligned.write_bytes(aligned.astype('<i2').tobytes())
        print(f'aligned capture written: {args.write_aligned} ({n_ref} frames, blocks of {block})')
    if args.expect:
        expect = np.frombuffer(args.expect.read_bytes(), dtype='<i2').reshape(-1, 2).astype(np.float64)[:n_ref]
        if args.warp_check:
            # Put the measured drift back on the expected audio, then take it off again: a correct warp returns the
            # expected audio. A low SNR here would mean the method, not the stream, limits the measurement.
            # the lag the simulation applies is a function of the capture index, so it is written as the same line
            # evaluated at (index - glob): intercept - slope * glob / RATE (ms), then the warp runs backwards
            simulated = fractional_warp(expect, -glob, -(intercept_ms - slope_ms * glob / RATE), -slope_ms, len(expect))
            back = fractional_warp(simulated, glob, intercept_ms, slope_ms, n_ref).clip(-32768, 32767)
            summary['warp_check_snr_db'] = float(10 * np.log10(
                np.sum(expect ** 2) / max(np.sum((back - expect) ** 2), 1e-9)))
        err = aligned[:len(expect)] - expect
        per_second = []
        for s in range(0, len(expect) - RATE, RATE):
            e, a = expect[s:s + RATE], aligned[s:s + RATE]
            per_second.append(10 * np.log10(np.sum(e ** 2) / max(np.sum((a - e) ** 2), 1e-9)) if np.sum(e ** 2) > 0 else None)
        valid = [x for x in per_second if x is not None]
        expect_report = dict(
            snr_db_overall=float(10 * np.log10(np.sum(expect ** 2) / max(np.sum(err ** 2), 1e-9))),
            max_abs_error=int(np.max(np.abs(err))), error_rms=float(np.sqrt(np.mean(err ** 2))),
            snr_db_per_second_min=float(min(valid)), snr_db_per_second_median=float(np.median(valid)),
            seconds_below_20_db=int(sum(x < 20 for x in valid)))
        summary['expect'] = expect_report
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(summary, indent=2))
    print(json.dumps({k: v for k, v in summary.items() if k != 'rows'}, indent=2))


if __name__ == '__main__':
    main()
