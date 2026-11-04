#!/usr/bin/env python3
"""Encode a stereo 44.1 kHz track as CRI ADX (encoding 3), the format LibSaturn's sat_music streams from the CD.

The source and the resampling are those of encode_ima_stream.py. FFmpeg's adpcm_adx encoder makes the frames (18 bytes
per channel for 32 samples, 4.5 bits per sample); the file is what sat_music_open expects: the two LPC coefficients
(S16BE, 4096 scale, derived from the header cutoff) followed by the frames, 4 + ceil(frames / 32) * 36 bytes.
decode() mirrors lib-saturn's src/audio/music/codecs/adx.hpp, so the file is checked without a Saturn.

Writes under --out (private, git-ignored), with the names run_ima_stream_prototype.py reads:
    ima.bin                   the ADX payload
    ima_meta.h                frame count, rate, size and IMA_FORMAT_ADX for the test program
    ima_decoded_44100.s16le   what an exact decoder plays
    encode.json               sizes and the codec's own error

    python3 scripts/encode_adx_stream.py --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav \\
        --out assets/imported/adx-proto/paul
"""
import argparse
import json
import math
import struct
import subprocess
from pathlib import Path

import numpy as np

import encode_ima_stream as ima

FRAME_SAMPLES = 32
FRAME_BYTES = 18


def encode(samples, ffmpeg='ffmpeg'):
    """samples: (frames, 2) int64. Returns the payload: 4 coefficient bytes, then the ADX frames."""
    frames = samples.shape[0]
    pcm = samples.astype('>i2').tobytes()
    command = [ffmpeg, '-v', 'error', '-f', 's16be', '-ar', str(ima.RATE), '-ac', '2', '-i', 'pipe:0',
               '-c:a', 'adpcm_adx', '-f', 'adx', 'pipe:1']
    encoded = subprocess.run(command, input=pcm, check=True, stdout=subprocess.PIPE).stdout
    if encoded[:2] != b'\x80\x00' or encoded[4:8] != bytes((3, 18, 4, 2)):
        raise SystemExit('FFmpeg did not produce ADX encoding 3')
    data_offset = int.from_bytes(encoded[2:4], 'big') + 4
    if encoded[data_offset - 6:data_offset] != b'(c)CRI':
        raise SystemExit('unexpected ADX header')
    cutoff = int.from_bytes(encoded[16:18], 'big')
    a = math.sqrt(2.0) - math.cos(2.0 * math.pi * cutoff / ima.RATE)
    b = math.sqrt(2.0) - 1.0
    c = (a - math.sqrt((a + b) * (a - b))) / b
    coefficients = (round(c * 2.0 * 4096), round(-c * c * 4096))
    body = -(-frames // FRAME_SAMPLES) * FRAME_BYTES * 2
    if len(encoded) < data_offset + body:
        raise SystemExit('truncated ADX output')
    return struct.pack('>hh', *coefficients) + encoded[data_offset:data_offset + body]


def decode(data, frames):
    """Mirror of adx.hpp decode_block over the whole file: (frames, 2) int64."""
    c0, c1 = struct.unpack('>hh', data[:4])
    groups = (len(data) - 4) // (2 * FRAME_BYTES)
    raw = np.frombuffer(data, dtype=np.uint8, offset=4).reshape(groups, 2, FRAME_BYTES)
    out = np.zeros((groups * FRAME_SAMPLES, 2), dtype=np.int64)
    for channel in range(2):
        scale = (raw[:, channel, 0].astype(np.int64) << 8) | raw[:, channel, 1]
        if np.any(scale & 0x8000):
            raise ValueError('an ADX end marker inside the payload')
        packed = raw[:, channel, 2:]
        nibbles = np.stack([packed >> 4, packed & 15], axis=-1).reshape(groups, FRAME_SAMPLES).astype(np.int64)
        deltas = np.where(nibbles < 8, nibbles, nibbles - 16) * scale[:, None]
        flat = deltas.reshape(-1).tolist()
        samples = [0] * len(flat)
        p1 = p2 = 0
        for n, d in enumerate(flat):
            s = d + ((c0 * p1 + c1 * p2) >> 12)
            s = -32768 if s < -32768 else 32767 if s > 32767 else s
            samples[n] = s
            p2, p1 = p1, s
        out[:, channel] = samples
    return out[:frames]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--seconds', type=float, help='encode only this much (default: the whole oracle)')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--rate', type=int, choices=(44100, 22050), default=22050,
                        help='output rate; 22050 (the fight\'s: docs/IMA_STREAM_EXPERIMENT.md) halves the SH-2 '
                             'decode and the disc bandwidth against 44100')
    args = parser.parse_args()
    ima.RATE = args.rate   # read_oracle resamples to it, and the encoder and the meta header follow

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    reference = ima.read_oracle(args.oracle, args.seconds)
    samples = ima.to_s16(reference)
    frames = samples.shape[0]
    data = encode(samples, args.ffmpeg)
    decoded = decode(data, frames)

    (out / 'ima.bin').write_bytes(data)
    (out / f'ima_decoded_{ima.RATE}.s16le').write_bytes(decoded.astype('<i2').tobytes())
    (out / 'ima_meta.h').write_text(
        f'#define IMA_SAMPLE_RATE {ima.RATE}u\n#define IMA_SAMPLE_COUNT {frames}u\n#define IMA_CHANNELS 2u\n'
        f'#define IMA_BYTES {len(data)}u\n#define IMA_FORMAT_ADX 1\n')
    raw_bytes = frames * 4
    summary = dict(
        oracle=str(args.oracle), frames=frames, seconds=frames / ima.RATE, adx_bytes=len(data),
        raw_pcm_bytes=raw_bytes, ratio=len(data) / raw_bytes, bits_per_sample=8 * len(data) / (frames * 2),
        codec_snr_db=float(ima.snr_db(reference[:frames], decoded.astype(np.float64))),
        codec_peak_error=int(np.max(np.abs(reference[:frames] - decoded))))
    (out / 'encode.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
