#!/usr/bin/env python3
"""Write a contact sheet of a SATMOV01 movie's frames, decoded the way LibSaturn's cinepak.cpp does."""
import argparse
import struct
from pathlib import Path

from PIL import Image

HEADER_BYTES = 2048


def read_header(data):
    if data[:8] != b'SATMOV01':
        raise ValueError('not a SATMOV01 movie')
    width, height, frames, frame_us = struct.unpack_from('>HHII', data, 8)
    return width, height, frames, frame_us


def entry_to_patch(entry):
    """Four RGB dots from four luma bytes and signed u, v (ffmpeg's cinepak rule)."""
    u, v = struct.unpack('bb', entry[4:6])
    clip = lambda value: max(0, min(255, value))
    return [(clip(y + 2 * v), clip(y - int(u / 2) - v), clip(y + 2 * u)) for y in entry[:4]]


def load_codebook(book, update, body):
    pos, flags, mask = 0, 0, 0
    for index in range(256):
        if update:
            mask >>= 1
            if mask == 0:
                if len(body) - pos < 4:
                    return
                flags = struct.unpack_from('>I', body, pos)[0]
                pos, mask = pos + 4, 0x80000000
            if not flags & mask:
                continue
        if len(body) - pos < 6:
            return
        book[index] = entry_to_patch(body[pos:pos + 6])
        pos += 6


def put_patch(pixels, x, y, patch, scale=1):
    for row in range(2):
        for col in range(2):
            colour = patch[row * 2 + col]
            for dy in range(scale):
                for dx in range(scale):
                    pixels[x + col * scale + dx, y + row * scale + dy] = colour


def decode_vectors(chunk, body, width, height, books, pixels):
    has_skip, v1_only = chunk & 1, chunk & 2
    pos, flags, mask = 0, 0, 0

    def next_flag():
        nonlocal pos, flags, mask
        mask >>= 1
        if mask == 0:
            flags = struct.unpack_from('>I', body, pos)[0]
            pos, mask = pos + 4, 0x80000000
        return flags & mask

    for y in range(0, height, 4):
        for x in range(0, width, 4):
            if has_skip and not next_flag():
                continue
            if v1_only or not next_flag():
                patch = books['v1'][body[pos]]
                pos += 1
                for row in range(2):
                    for col in range(2):
                        put_patch(pixels, x + col * 2, y + row * 2, [patch[row * 2 + col]] * 4)
            else:
                put_patch(pixels, x, y, books['v4'][body[pos]])
                put_patch(pixels, x + 2, y, books['v4'][body[pos + 1]])
                put_patch(pixels, x, y + 2, books['v4'][body[pos + 2]])
                put_patch(pixels, x + 2, y + 2, books['v4'][body[pos + 3]])
                pos += 4


def decode_frame(frame, width, height, books, pixels):
    pos = 10
    strip_bytes = struct.unpack_from('>H', frame, pos + 2)[0]
    end, pos = pos + strip_bytes, pos + 12
    while end - pos >= 4:
        chunk, size = frame[pos], struct.unpack_from('>H', frame, pos + 2)[0]
        body = frame[pos + 4:pos + size]
        if chunk in (0x20, 0x21):
            load_codebook(books['v4'], chunk == 0x21, body)
        elif chunk in (0x22, 0x23):
            load_codebook(books['v1'], chunk == 0x23, body)
        elif chunk in (0x30, 0x31, 0x32):
            decode_vectors(chunk, body, width, height, books, pixels)
        pos += size


def contact_sheet(path, every_seconds, columns):
    data = Path(path).read_bytes()
    width, height, frames, frame_us = read_header(data)
    picture = Image.new('RGB', (width, height))
    pixels = picture.load()
    books = {'v1': [[(0, 0, 0)] * 4 for _ in range(256)], 'v4': [[(0, 0, 0)] * 4 for _ in range(256)]}
    step = max(1, round(every_seconds * 1000000 / frame_us))
    shots, pos = [], HEADER_BYTES
    for index in range(frames):
        video_bytes, audio_bytes = struct.unpack_from('>II', data, pos)
        start = pos + 8 + audio_bytes
        decode_frame(data[start:start + video_bytes], width, height, books, pixels)
        pos = start + video_bytes + ((4 - (video_bytes & 3)) & 3)
        if index % step == 0:
            shots.append(picture.copy())
    rows = (len(shots) + columns - 1) // columns
    sheet = Image.new('RGB', (columns * width, rows * height))
    for index, shot in enumerate(shots):
        sheet.paste(shot, (index % columns * width, index // columns * height))
    return sheet, dict(width=width, height=height, frames=frames, seconds=frames * frame_us / 1e6, shots=len(shots))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('movie', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--every', type=float, default=1.0, help='seconds between shots')
    parser.add_argument('--columns', type=int, default=8)
    args = parser.parse_args()
    sheet, report = contact_sheet(args.movie, args.every, args.columns)
    sheet.save(args.out)
    print(report)


if __name__ == '__main__':
    main()
