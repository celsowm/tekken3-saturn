#!/usr/bin/env python3
"""Encode a stereo 44.1 kHz track as blocked IMA ADPCM, the format sat_music streams from the CD.

The source is the libvgm C352 oracle WAV (88.2 kHz stereo), brought to 44.1 kHz with the same polyphase low-pass
compare_c352_scsp.py uses for its reference. Each 1024-frame block holds, per channel, a 4-byte header (the first
sample as S16BE, the step index, a pad byte) and then 1023 4-bit codes, high nibble first, the two channels
alternating within a byte, padded to 4 bytes: 1032 bytes for stereo. The decoder is the one in lib-saturn
(src/audio/playback/ima_adpcm.hpp), mirrored here by decode_blocks() so the file can be checked without a Saturn.

Writes (under --out, private and git-ignored):
    ima.bin                   the blocks; the file size is ceil(frames / 1024) * 1032, as sat_music requires
    ima_meta.h                the frame count, rate and size the test program is built with
    ima_decoded_44100.s16le   the decoded blocks: what an exact decoder plays (a software control for compare)
    encode.json               sizes, the codec's own error, and the ratio to raw PCM

    python3 scripts/encode_ima_stream.py --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav \\
        --out assets/imported/ima-proto/paul
"""
import argparse
import json
import wave
from pathlib import Path

import numpy as np
from scipy import signal

RATE = 44100
BLOCK_FRAMES = 1024
BLOCK_BYTES = 1032

STEPS = np.array([
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767,
], dtype=np.int64)
INDEX_DELTA = np.array([-1, -1, -1, -1, 2, 4, 6, 8], dtype=np.int64)


def tables():
    """magnitude[index, code & 7] and next_index[index, code & 7], as the decoder builds them."""
    magnitude = np.zeros((89, 8), dtype=np.int64)
    next_index = np.zeros((89, 8), dtype=np.int64)
    for index in range(89):
        step = int(STEPS[index])
        for code in range(8):
            delta = step >> 3
            if code & 4:
                delta += step
            if code & 2:
                delta += step >> 1
            if code & 1:
                delta += step >> 2
            magnitude[index, code] = delta
            next_index[index, code] = min(max(index + int(INDEX_DELTA[code]), 0), 88)
    return magnitude, next_index


MAGNITUDE, NEXT_INDEX = tables()


def read_oracle(path, seconds=None):
    with wave.open(str(path), 'rb') as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 2, 'stereo 16-bit expected'
        rate = w.getframerate()
        assert rate % RATE == 0, 'the oracle rate must be a multiple of 44100'
        frames = w.readframes(w.getnframes() if seconds is None else int(seconds * rate))
    x = np.frombuffer(frames, dtype='<i2').reshape(-1, 2).astype(np.float64)
    return signal.resample_poly(x, 1, rate // RATE, axis=0)


def to_s16(x):
    return np.clip(np.round(x), -32768, 32767).astype(np.int64)


def initial_index(block):
    """block: (blocks, channel, frame). Per block and channel: the step nearest the mean first difference."""
    mean_delta = np.abs(np.diff(block, axis=2)).mean(axis=2)                  # (blocks, 2)
    return np.abs(STEPS[None, None, :] - mean_delta[..., None]).argmin(axis=-1)


def encode(samples):
    """samples: (frames, 2) int64. Returns (blocks bytes, the encoder's own reconstruction (frames', 2))."""
    frames = samples.shape[0]
    blocks = -(-frames // BLOCK_FRAMES)
    padded = np.zeros((blocks * BLOCK_FRAMES, 2), dtype=np.int64)
    padded[:frames] = samples
    x = padded.reshape(blocks, BLOCK_FRAMES, 2).transpose(0, 2, 1)           # (blocks, channel, frame)
    predictor = x[:, :, 0].copy()                                            # exact: the header sample
    index = initial_index(x)
    codes = np.zeros((blocks, 2, BLOCK_FRAMES), dtype=np.int64)
    recon = np.zeros((blocks, 2, BLOCK_FRAMES), dtype=np.int64)
    recon[:, :, 0] = predictor
    start_index = index.copy()
    for i in range(1, BLOCK_FRAMES):
        mags = MAGNITUDE[index]                                              # (blocks, 2, 8)
        candidates = np.arange(16)
        mag = mags[..., candidates & 7]                                      # (blocks, 2, 16)
        sign = np.where(candidates & 8, -1, 1)
        trial = np.clip(predictor[..., None] + sign * mag, -32768, 32767)
        error = np.abs(x[:, :, i][..., None] - trial)
        code = error.argmin(axis=-1)                                         # ties go to the smaller code
        predictor = np.take_along_axis(trial, code[..., None], axis=-1)[..., 0]
        index = NEXT_INDEX[index, code & 7]
        codes[:, :, i] = code
        recon[:, :, i] = predictor
    out = np.zeros((blocks, BLOCK_BYTES), dtype=np.uint8)
    for channel in range(2):
        head = 4 * channel
        first = x[:, channel, 0] & 0xFFFF                                    # two's complement, S16BE
        out[:, head] = first >> 8
        out[:, head + 1] = first & 0xFF
        out[:, head + 2] = start_index[:, channel]
    # payload: byte k holds the left code of frame k+1 in its high nibble and the right code in its low nibble
    payload = ((codes[:, 0, 1:] << 4) | codes[:, 1, 1:]).astype(np.uint8)
    out[:, 8:8 + payload.shape[1]] = payload
    return out.tobytes(), recon.transpose(0, 2, 1).reshape(-1, 2)[:frames]


def decode_blocks(data, frames):
    """The lib-saturn decoder, block by block: channel-planar output, then interleaved (frames, 2) int64."""
    blocks = len(data) // BLOCK_BYTES
    raw = np.frombuffer(data, dtype=np.uint8).reshape(blocks, BLOCK_BYTES)
    out = np.zeros((blocks, 2, BLOCK_FRAMES), dtype=np.int64)
    for channel in range(2):
        head = raw[:, 4 * channel:4 * channel + 4].astype(np.int64)
        predictor = ((head[:, 0] << 8) | head[:, 1])
        predictor = np.where(predictor >= 32768, predictor - 65536, predictor)
        index = head[:, 2]
        if np.any(index > 88):
            raise ValueError('step index above 88 in a block header')
        out[:, channel, 0] = predictor
        for i in range(1, BLOCK_FRAMES):
            packed = raw[:, 8 + i - 1].astype(np.int64)
            code = (packed >> 4) if channel == 0 else (packed & 15)
            delta = MAGNITUDE[index, code & 7]
            predictor = np.clip(predictor + np.where(code & 8, -delta, delta), -32768, 32767)
            index = NEXT_INDEX[index, code & 7]
            out[:, channel, i] = predictor
    return out.transpose(0, 2, 1).reshape(-1, 2)[:frames]


def snr_db(reference, decoded):
    noise = reference - decoded
    return 10 * np.log10(np.sum(reference ** 2) / max(np.sum(noise ** 2), 1e-9))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--seconds', type=float, help='encode only this much (default: the whole oracle)')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    reference = read_oracle(args.oracle, args.seconds)
    samples = to_s16(reference)
    frames = samples.shape[0]
    data, recon = encode(samples)
    decoded = decode_blocks(data, frames)
    if not np.array_equal(decoded, recon):
        raise SystemExit('decoder mirror and encoder reconstruction differ: the block format is wrong')

    (out / 'ima.bin').write_bytes(data)
    pcm = decoded.astype('<i2').tobytes()
    (out / 'ima_decoded_44100.s16le').write_bytes(pcm)
    (out / 'ima_meta.h').write_text(
        f'#define IMA_SAMPLE_RATE {RATE}u\n'
        f'#define IMA_SAMPLE_COUNT {frames}u\n'
        f'#define IMA_CHANNELS 2u\n'
        f'#define IMA_BYTES {len(data)}u\n')
    raw_bytes = frames * 4
    summary = dict(
        oracle=str(args.oracle), frames=frames, seconds=frames / RATE, blocks=len(data) // BLOCK_BYTES,
        ima_bytes=len(data), raw_pcm_bytes=raw_bytes, ratio=len(data) / raw_bytes,
        bits_per_sample=8 * len(data) / (frames * 2),
        codec_snr_db=float(snr_db(reference[:frames], decoded.astype(np.float64))),
        codec_peak_error=int(np.max(np.abs(reference[:frames] - decoded))))
    (out / 'encode.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
