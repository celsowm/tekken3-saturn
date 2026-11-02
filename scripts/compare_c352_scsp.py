#!/usr/bin/env python3
"""Compare an SCSP capture (Ymir, 44100 Hz stereo s16le) with the libvgm C352 oracle WAV (88200 Hz stereo).

Both are brought to 44100 Hz (the oracle through a polyphase low-pass, so content the SCSP cannot carry above
22 kHz is removed from the reference too). The capture is aligned to the oracle by cross-correlation of the
mono mixes; the lag is then tracked in 1 s blocks to expose drift and per-block timing error. Chips differ in
interpolation, envelope and loop handling, so nothing here asks for bit-exact equality: the metrics describe
how far apart they are in level, pitch, spectrum, stereo placement and envelope shape.

Writes (under --out): reference_44100.wav and scsp_aligned.wav (same length, same start, for A/B listening),
scsp_gain_matched.wav (the capture scaled to the oracle's RMS), compare.json."""
import argparse
import json
import math
import wave
from pathlib import Path

import numpy as np
from scipy import signal, stats

RATE = 44100


def read_wav(path, seconds):
    with wave.open(str(path), 'rb') as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 2, 'stereo 16-bit expected'
        rate = w.getframerate()
        frames = w.readframes(int(seconds * rate))
    x = np.frombuffer(frames, dtype='<i2').reshape(-1, 2).astype(np.float64)
    return x, rate


def write_wav(path, x):
    pcm = np.clip(np.round(x), -32768, 32767).astype('<i2')
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def rms(x):
    return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0


def db(a, b=1.0):
    return 20 * math.log10(max(a, 1e-9) / max(b, 1e-9))


def best_lag(ref, cap, lo, hi):
    """Lag (samples) at which cap[n + lag] best matches ref[n]; searched in [lo, hi]."""
    n = min(len(ref), len(cap) - hi)
    r = ref[:n] - ref[:n].mean()
    seg = cap[lo:hi + n]
    corr = signal.correlate(seg - seg.mean(), r, mode='valid', method='fft')
    norm = np.sqrt(np.convolve(seg ** 2, np.ones(n), mode='valid') * np.sum(r ** 2)) + 1e-9
    score = corr / norm
    k = int(np.argmax(score))
    return lo + k, float(score[k])


def peaks(frame, rate, count=6, fmin=80, fmax=6000):
    win = np.hanning(len(frame))
    spec = np.abs(np.fft.rfft(frame * win))
    freqs = np.fft.rfftfreq(len(frame), 1 / rate)
    band = (freqs >= fmin) & (freqs <= fmax)
    idx = np.where(band)[0]
    local = [i for i in idx[1:-1] if spec[i] > spec[i - 1] and spec[i] >= spec[i + 1] and spec[i] > 0]
    top = sorted(local, key=lambda i: -spec[i])[:count]
    out = []
    for i in top:                              # parabolic refinement on the log spectrum
        a, b, c = (math.log(spec[i + k] + 1e-9) for k in (-1, 0, 1))
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) else 0.0
        out.append((freqs[i] + d * (freqs[1] - freqs[0]), spec[i]))
    return sorted(out)


def pitch_summary(e):
    return dict(pairs=len(e), cents_median=float(np.median(e)) if e else None,
                cents_mean_abs=float(np.mean(np.abs(e))) if e else None,
                cents_p95_abs=float(np.percentile(np.abs(e), 95)) if e else None)


def pitch_errors(ref, cap, rate, block=0.5):
    errors = []
    size = int(block * rate) // 2 * 2
    for s in range(0, len(ref) - size, size):
        a, b = peaks(ref[s:s + size], rate), peaks(cap[s:s + size], rate)
        if not a or not b or rms(ref[s:s + size]) < 200:
            continue
        for f, mag in a[:3]:
            g = min(b, key=lambda p: abs(1200 * math.log2(p[0] / f)))
            cents = 1200 * math.log2(g[0] / f)
            if abs(cents) < 50:
                errors.append(cents)
    return errors


OCTAVES = [(62, 125), (125, 250), (250, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000), (16000, 22050)]


def band_levels(x, rate):
    f, p = signal.welch(x, rate, nperseg=4096)
    return [10 * math.log10(max(float(p[(f >= lo) & (f < hi)].sum()), 1e-12)) for lo, hi in OCTAVES]


def spectrogram_similarity(a, b, rate=RATE, fmin=100, fmax=8000):
    """Phase-blind match: cosine similarity of the log-magnitude STFT frames (about 93 ms), mean and per second."""
    f, t, za = signal.stft(a, rate, nperseg=4096, noverlap=2048)
    _, _, zb = signal.stft(b, rate, nperseg=4096, noverlap=2048)
    band = (f >= fmin) & (f <= fmax)
    la, lb = np.log10(np.abs(za[band]) + 1.0), np.log10(np.abs(zb[band]) + 1.0)
    la, lb = la - la.mean(axis=0), lb - lb.mean(axis=0)
    cos = np.sum(la * lb, axis=0) / (np.linalg.norm(la, axis=0) * np.linalg.norm(lb, axis=0) + 1e-9)
    energy = np.sqrt(np.mean(np.abs(za[band]) ** 2, axis=0))
    live = energy > 2.0
    per_second = [float(cos[(t >= s) & (t < s + 1) & live].mean()) if np.any((t >= s) & (t < s + 1) & live) else None
                  for s in range(int(t[-1]))]
    return float(cos[live].mean()), per_second


def waveform_by_block(ref, cap, block=RATE):
    """Zero-lag correlation per second: coherent only while the two renderings stay in phase."""
    return [float(np.corrcoef(ref[s:s + block], cap[s:s + block])[0, 1]) for s in range(0, len(ref) - block + 1, block)]


def envelope(x, hop):
    n = len(x) // hop
    return np.sqrt(np.mean(x[:n * hop].reshape(n, hop) ** 2, axis=1))


def compare(oracle, capture, seconds, out):
    ref88, rate = read_wav(oracle, seconds + 1.0)
    ref = signal.resample_poly(ref88, 1, rate // RATE, axis=0)
    raw = np.frombuffer(Path(capture).read_bytes(), dtype='<i2').reshape(-1, 2).astype(np.float64)
    n_ref = int(seconds * RATE)
    ref_mono, cap_mono = ref.sum(axis=1), raw.sum(axis=1)
    # The mix is periodic (bass loops repeat every ~32 ms), so a global correlation can lock on a neighbouring
    # peak. The first sound after silence is unambiguous: align the starts, then refine over the whole window within +-8 ms.
    first = lambda x: int(np.argmax(np.abs(x) > 0.05 * np.max(np.abs(x[:RATE]))))
    coarse = first(cap_mono) - first(ref_mono)
    lag, peak_corr = best_lag(ref_mono[:n_ref], cap_mono, max(0, coarse - int(0.008 * RATE)), coarse + int(0.008 * RATE))
    cap = raw[lag:lag + n_ref + int(0.3 * RATE)]
    ref = ref[:len(cap)]
    n = min(n_ref, len(cap), len(ref))
    cap_w, ref_w = cap[:n], ref[:n]
    rms_l, rms_r = rms(cap_w[:, 0]), rms(cap_w[:, 1])
    ref_l, ref_r = rms(ref_w[:, 0]), rms(ref_w[:, 1])
    gain = rms(cap_w) / max(rms(ref_w), 1e-9)

    # local lag per 1 s block, searched +-25 ms around the global lag
    blocks, lags, corrs = [], [], []
    for s in range(0, n - RATE, RATE // 2):
        a = ref_mono[s:s + RATE]
        if rms(a) < 150:
            continue
        lo = max(0, lag + s - int(0.008 * RATE))
        l, c = best_lag(a, cap_mono[:lag + s + RATE + int(0.05 * RATE)], lo, lag + s + int(0.008 * RATE))
        blocks.append(s / RATE)
        lags.append((l - lag - s) / RATE * 1000)
        corrs.append(c)
    fit = stats.linregress(blocks, lags) if len(blocks) > 2 else None
    slope, slope_err = (float(fit.slope), float(fit.stderr)) if fit else (0.0, None)

    cap_m, ref_m = cap_w.sum(axis=1), ref_w.sum(axis=1)
    zero_lag = float(np.corrcoef(cap_m, ref_m)[0, 1])
    env_c, env_r = envelope(cap_m, 441), envelope(ref_m, 441)           # 10 ms
    live = env_r > 100
    env_db = 20 * np.log10((env_c[live] + 1e-3) / (env_r[live] + 1e-3))
    env_corr = float(np.corrcoef(env_c, env_r)[0, 1])

    spec_sim, spec_sim_s = spectrogram_similarity(ref_m, cap_m)
    pe = pitch_errors(ref_m, cap_m, RATE, 1.0)
    pe2 = pitch_errors(ref_m, cap_m, RATE, 2.0)
    bl_r, bl_c = band_levels(ref_m, RATE), band_levels(cap_m, RATE)

    def pan(x):
        l, r = rms(x[:, 0]), rms(x[:, 1])
        return db(l, r)

    pan_blocks = []
    for s in range(0, n - RATE, RATE):
        a, b = ref_w[s:s + RATE], cap_w[s:s + RATE]
        if rms(a) > 150:
            pan_blocks.append((s / RATE, pan(a), pan(b)))
    side = lambda x: rms(x[:, 0] - x[:, 1]) / max(rms(x[:, 0] + x[:, 1]), 1e-9)

    out.mkdir(parents=True, exist_ok=True)
    write_wav(out / 'reference_44100.wav', ref_w)
    write_wav(out / 'scsp_aligned.wav', cap_w)
    write_wav(out / 'scsp_gain_matched.wav', cap_w / max(gain, 1e-9))
    result = dict(
        window_s=seconds, capture_lag_samples=lag, capture_lag_s=lag / RATE, alignment_correlation=peak_corr,
        timing=dict(blocks=len(blocks), local_lag_ms_mean=float(np.mean(lags)), local_lag_ms_std=float(np.std(lags)),
                    local_lag_ms_max_abs=float(np.max(np.abs(lags))), local_corr_min=float(np.min(corrs)) if corrs else None, drift_ms_per_s=slope, drift_stderr_ms_per_s=slope_err,
                    drift_over_window_ms=slope * seconds,
                    note='1 s blocks, lag searched within +-8 ms of the start alignment; tick resolution is 5.805 ms'),
        level=dict(rms_scsp=[rms_l, rms_r], rms_oracle=[ref_l, ref_r], rms_ratio_db=db(rms(cap_w), rms(ref_w)),
                   peak_scsp=float(np.max(np.abs(cap_w))), peak_oracle=float(np.max(np.abs(ref_w))),
                   peak_ratio_db=db(float(np.max(np.abs(cap_w))), float(np.max(np.abs(ref_w))))),
        correlation=dict(zero_lag_mono=zero_lag, zero_lag_per_second=waveform_by_block(ref_m, cap_m),
                         spectrogram_cosine=spec_sim, spectrogram_cosine_per_second=spec_sim_s, envelope_10ms=env_corr,
                         envelope_error_db_mean=float(np.mean(env_db)), envelope_error_db_std=float(np.std(env_db)),
                         envelope_error_db_p95_abs=float(np.percentile(np.abs(env_db), 95))),
        pitch=dict(block_1s=pitch_summary(pe), block_2s=pitch_summary(pe2),
                   note='strongest partials of the oracle matched to the nearest capture peak; longer blocks resolve '
                        'the pad ensemble, whose voices beat a few cents apart'),
        spectrum=dict(octave_bands_hz=OCTAVES, oracle_db=bl_r, scsp_db=bl_c,
                      difference_db=[c - r for c, r in zip(bl_c, bl_r)]),
        stereo=dict(lr_ratio_db_oracle=pan(ref_w), lr_ratio_db_scsp=pan(cap_w),
                    side_to_mid_oracle=side(ref_w), side_to_mid_scsp=side(cap_w),
                    per_second=[dict(t=t, oracle_db=a, scsp_db=b) for t, a, b in pan_blocks]),
        files=['reference_44100.wav', 'scsp_aligned.wav', 'scsp_gain_matched.wav'])
    (out / 'compare.json').write_text(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True, help='Ymir --dump-audio output (stereo s16le, 44100 Hz)')
    parser.add_argument('--seconds', type=float, default=10.0)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    r = compare(args.oracle, args.capture, args.seconds, args.out)
    print(json.dumps({k: v for k, v in r.items() if k != 'stereo'}, indent=2))


if __name__ == '__main__':
    main()
