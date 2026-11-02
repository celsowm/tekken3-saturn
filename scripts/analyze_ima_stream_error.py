#!/usr/bin/env python3
"""Where the error of a stream capture sits, relative to the ADPCM block and the disc segment.

Input: a capture already moved onto the expected timeline (analyze_ima_stream_lag.py --write-aligned) and the exact
decode it should equal (ima_decoded_44100.s16le). The error is grouped two ways:

  - by position inside an ADPCM block (frame mod 1024): a decoder that starts a block wrong shows up here;
  - by position inside a disc segment (8192 bytes = frame mod 8127.9 frames): a carry between two reads shows up here.

A flat profile in both means the error is not tied to the transport. Writes the profile to --out (JSON).

    python3 scripts/analyze_ima_stream_error.py --aligned assets/imported/ima-proto/run/aligned.s16le \\
        --expect assets/imported/ima-proto/paul/ima_decoded_44100.s16le --out assets/imported/ima-proto/run/error.json
"""
import argparse
import json
from pathlib import Path

import numpy as np

BLOCK = 1024
SEGMENT_FRAMES = 8192 / 1032 * BLOCK           # one 8192-byte segment of IMA blocks, in frames
BINS = 16


def read_s16(path):
    return np.frombuffer(path.read_bytes(), dtype='<i2').reshape(-1, 2).astype(np.float64)


def snr_by_bin(frame_ref, frame_err, phase, bins):
    """frame_ref, frame_err: energy per frame. The SNR of each phase bin is the ratio of its summed energies."""
    out = []
    for b in range(bins):
        sel = (phase >= b / bins) & (phase < (b + 1) / bins)
        out.append(round(10 * np.log10(frame_ref[sel].sum() / max(frame_err[sel].sum(), 1e-9)), 2))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--aligned', type=Path, required=True)
    parser.add_argument('--expect', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    expect = read_s16(args.expect)
    aligned = read_s16(args.aligned)[:len(expect)]
    expect = expect[:len(aligned)]
    error = aligned - expect
    n = np.arange(len(expect), dtype=np.float64)
    per_frame_ref = np.sum(expect ** 2, axis=1)
    per_frame_err = np.sum(error ** 2, axis=1)
    block_phase = (n % BLOCK) / BLOCK
    segment_phase = (n % SEGMENT_FRAMES) / SEGMENT_FRAMES
    report = dict(
        frames=len(expect), overall_snr_db=round(float(10 * np.log10(per_frame_ref.sum() / per_frame_err.sum())), 2),
        snr_by_block_phase_db=snr_by_bin(per_frame_ref, per_frame_err, block_phase, BINS),
        snr_by_segment_phase_db=snr_by_bin(per_frame_ref, per_frame_err, segment_phase, BINS),
        segment_frames=round(SEGMENT_FRAMES, 3))
    # the error is also reported per second, so a stretch that goes bad is visible without the averages
    rate = 44100
    per_second = []
    for s in range(0, len(expect) - rate, rate):
        ref, err = per_frame_ref[s:s + rate].sum(), per_frame_err[s:s + rate].sum()
        per_second.append(round(float(10 * np.log10(ref / max(err, 1e-9))), 2) if ref > 0 else None)
    report['snr_per_second_db'] = per_second
    # a single gain that best fits the capture to the expected audio, and the SNR left after it
    gain = float(np.sum(aligned * expect) / np.sum(expect ** 2))
    report['best_gain'] = round(gain, 5)
    report['snr_after_gain_db'] = round(float(10 * np.log10(np.sum(expect ** 2) / np.sum((aligned - gain * expect) ** 2))), 2)
    # where the error lives: energy of expected and error per band, the whole file in one spectrum (mono sums)
    mono_e, mono_err = expect.sum(axis=1), error.sum(axis=1)
    freq = np.fft.rfftfreq(len(mono_e), 1 / 44100)
    pe, pr = np.abs(np.fft.rfft(mono_err)) ** 2, np.abs(np.fft.rfft(mono_e)) ** 2
    bands = {'0-1k': (0, 1000), '1-4k': (1000, 4000), '4-8k': (4000, 8000), '8-16k': (8000, 16000), '16-22k': (16000, 22050)}
    report['band_snr_db'] = {name: round(float(10 * np.log10(pr[(freq >= lo) & (freq < hi)].sum() /
                                                             max(pe[(freq >= lo) & (freq < hi)].sum(), 1e-9))), 2)
                             for name, (lo, hi) in bands.items()}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2))
    print(json.dumps({k: v for k, v in report.items() if k != 'snr_per_second_db'}, indent=2))


if __name__ == '__main__':
    main()
