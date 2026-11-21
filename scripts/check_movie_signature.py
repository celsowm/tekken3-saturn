#!/usr/bin/env python3
"""Host-side packet signature for LibSaturn SATMOV01 movies.

Compares with the HASH and PACKETS printed by T3_MOVIE_DIAGNOSTICS on real Saturn/SAROO.
The digest covers the complete consumed packet bytes (audio, video and padding).
It is an inexpensive diagnostic checksum, NOT a cryptographic integrity guarantee.
"""
import argparse
import struct
from pathlib import Path

MAX_PACKET = 24 * 1024
IMA_BLOCK = 1032
MAGIC = b"SATMOV01"
SEED = 0x811C9DC5
STEP = 0x9E3779B9


def signature(path: Path):
    data = path.read_bytes()
    if len(data) < 2048 or data[:8] != MAGIC:
        raise ValueError(f"{path}: invalid SATMOV01 header")
    frames, packet_bytes = struct.unpack_from(">I", data, 12)[0], struct.unpack_from(">I", data, 32)[0]
    end = 2048 + packet_bytes
    if not frames or end != len(data):
        raise ValueError(f"{path}: packet payload/header size mismatch")
    has_sound = struct.unpack_from(">I", data, 20)[0] == 22050
    pos, checksum = 2048, SEED
    for index in range(frames):
        if pos + 8 > end:
            raise ValueError(f"{path}: truncated packet header at frame {index}")
        video, audio = struct.unpack_from(">II", data, pos)
        padding = (-video) & 3
        packet = 8 + audio + video + padding
        if (video < 10 or packet > MAX_PACKET or audio % IMA_BLOCK or
                (not has_sound and audio)):
            raise ValueError(f"{path}: invalid packet at frame {index}")
        if pos + packet > end:
            raise ValueError(f"{path}: truncated packet at frame {index}")
        for off in range(pos, pos + packet, 4):
            word = struct.unpack_from(">I", data, off)[0]
            checksum = (((checksum << 5) | (checksum >> 27)) ^ word) & 0xFFFFFFFF
            checksum = (checksum + STEP) & 0xFFFFFFFF
        pos += packet
    if pos != end:
        raise ValueError(f"{path}: trailing/uncounted bytes after frame {frames - 1}")
    return frames, checksum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("movies", nargs="+", type=Path)
    args = parser.parse_args()
    for movie in args.movies:
        frames, checksum = signature(movie)
        print(f"{movie.name} PACKETS={frames} HASH={checksum} HEX=0x{checksum:08X}")


if __name__ == "__main__":
    main()
