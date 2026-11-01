#!/usr/bin/env python3
"""Find the STR movies in TEKKEN3.XAS of the USA disc and cut one out as raw sectors.

A movie is a run of Form 1 sectors carrying MDEC frame chunks (STR header 0x0160, type 0x8001)
with XA audio sectors (Form 2) between them. The scan groups them by frame number, as
third_party/Tekken3Remake/tools/research/inspect_xas.py does (docs/research/formats/
sound-and-video.md, "STR movies"). `cut` returns the raw 2352-byte sectors of one movie, audio
lead-in and tail included, which `ffmpeg -f psxstr` reads. Nothing is written to disk.
"""
import argparse
import mmap
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import psx_disc_import as disc_import  # noqa: E402

RAW, SUBHEADER, USER = 2352, 16, 24
STR_MAGIC, STR_TYPE = 0x0160, 0x8001
AUDIO_LEAD, AUDIO_TAIL = 70, 310         # the audio starts up to 65 sectors before and ends up to 305 after
TRACK1 = ROOT / 'roms/Tekken 3 USA/Tekken 3 (USA) (Track 1).bin'


@dataclass
class Movie:
    first: int              # XAS sector of the first video sector
    last: int = 0
    frames: int = 0
    width: int = 0
    height: int = 0
    video_sectors: int = 0


def xas_extent(track1):
    with disc_import.Disc(track1) as disc:
        _, lba, size, _ = disc.find('TEKKEN3', 'TEKKEN3.XAS')
    return lba, (size + 2047) // 2048


def scan(image, xas_lba, sectors):
    movies, current, last_frame = [], None, 0
    for i in range(sectors):
        base = (xas_lba + i) * RAW
        if image[base + SUBHEADER + 2] & 0x20:      # Form 2: XA audio
            continue
        magic, kind, _chunk, _chunks, frame, _size, width, height = struct.unpack_from(
            '<HHHHIIHH', image, base + USER)
        if magic != STR_MAGIC or kind != STR_TYPE:
            continue
        if current is None or frame < last_frame or frame > last_frame + 1 or i - current.last > 16:
            current = Movie(first=i, width=width, height=height)
            movies.append(current)
        if frame != last_frame or current.frames == 0:
            current.frames += 1
        current.video_sectors += 1
        current.last = i
        last_frame = frame
    return movies


def open_movies(track1=TRACK1):
    lba, sectors = xas_extent(track1)
    handle = track1.open('rb')
    image = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
    return handle, image, lba, scan(image, lba, sectors)


def cut(image, xas_lba, movie, sectors, movies):
    """Raw sectors of one movie with its audio lead-in and tail, stopping short of the neighbouring movies."""
    before = max((m.last for m in movies if m.last < movie.first), default=-1)
    after = min((m.first for m in movies if m.first > movie.last), default=sectors)
    first = max(before + 1, movie.first - AUDIO_LEAD)
    last = min(after - 1, movie.last + AUDIO_TAIL)
    return image[(xas_lba + first) * RAW:(xas_lba + last + 1) * RAW]


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--track1', type=Path, default=TRACK1)
    args = p.parse_args()
    handle, image, lba, movies = open_movies(args.track1)
    print(f'XAS at LBA {lba}')
    for n, m in enumerate(movies):
        print(f'{n:2d} sector {m.first:7d}..{m.last:7d} frames {m.frames:5d} {m.width}x{m.height} '
              f'video sectors {m.video_sectors}')
    image.close()
    handle.close()


if __name__ == '__main__':
    main()
