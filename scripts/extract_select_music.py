#!/usr/bin/env python3
"""Decode the original character select music (XA stream 0x11) from the USA disc.

The XA stream table in SLUS_004.02 (rows of start, end, channel; sound.md and
tools/remake_import/media.py in the pinned Tekken3Remake) names the stream's
sectors inside TEKKEN3.XAS: every 8th sector from `start` to `end`, offset by the
channel. FFmpeg decodes the raw sectors (-f psxstr); the result is looped or
cut to `--seconds` as 44.1 kHz stereo WAV for the mixed-mode disc. Private
output: it stays under assets/imported or build.
"""
import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import psx_disc_import as disc_import  # noqa: E402

STREAM, INTERLEAVE, RAW_SECTOR = 0x11, 8, 2352


def find_table(exe):
    """Offset of the 50-row (start, end, channel) table: 12-byte rows that all look like streams."""
    for offset in range(0, len(exe) - 12 * 50, 4):
        rows = [struct.unpack_from('<3I', exe, offset + 12 * i) for i in range(50)]
        if all(r[0] < r[1] < 300000 and r[2] < 32 for r in rows):
            return offset
    raise ValueError('XA stream table not found in the executable')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--track1', type=Path, default=ROOT / 'roms/Tekken 3 USA/Tekken 3 (USA) (Track 1).bin')
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--out', type=Path, required=True, help='WAV path under assets/imported or build')
    p.add_argument('--seconds', type=int, default=60)
    p.add_argument('--ffmpeg', default='ffmpeg')
    args = p.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents for root in (ROOT / 'assets/imported', ROOT / 'build')):
        p.error('output must be under assets/imported or build')
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != disc_import.US_EXE_SHA:
        p.error('needs the fingerprinted USA SLUS_004.02')
    start, end, channel = struct.unpack_from('<3I', exe, find_table(exe) + 12 * STREAM)
    with disc_import.Disc(args.track1) as disc:
        _, lba, _, _ = disc.find('TEKKEN3', 'TEKKEN3.XAS')
        with args.track1.open('rb') as image:
            raw = bytearray()
            for sector in range(start, end + 1, INTERLEAVE):
                image.seek((lba + sector + channel) * RAW_SECTOR)
                raw += image.read(RAW_SECTOR)
    with tempfile.TemporaryDirectory() as tmp:
        source, decoded = Path(tmp) / 'in.str', Path(tmp) / 'out.wav'
        source.write_bytes(raw)
        subprocess.run([args.ffmpeg, '-y', '-loglevel', 'error', '-f', 'psxstr', '-i', str(source),
                        '-map', '0:a:0', '-ar', '44100', '-ac', '2', '-c:a', 'pcm_s16le', str(decoded)],
                       check=True)
        with wave.open(str(decoded), 'rb') as w:
            frames = w.readframes(w.getnframes())
    need = args.seconds * 44100 * 4
    frames = (frames * (need // len(frames) + 1))[:need]        # loop, then cut: it is a looping track
    out.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(out), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(frames)
    print(f'{out}: stream {STREAM:#x}, {args.seconds} s, lba {lba}, sectors {start}..{end} ch {channel}')


if __name__ == '__main__':
    main()
