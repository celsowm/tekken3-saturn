#!/usr/bin/env python3
"""Offline regression for the on-Saturn SATMOV01 packet signature."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from check_movie_signature import signature  # noqa: E402


def packet(video: bytes, audio=b""):
    return (struct.pack(">II", len(video), len(audio)) + audio +
            video + b"\x00" * ((-len(video)) & 3))


def movie(path: Path, frames, audio_rate=22050, count=None):
    payload = b"".join(frames)
    hdr = bytearray(2048)
    hdr[:8] = b"SATMOV01"
    struct.pack_into(">I", hdr, 12, len(frames) if count is None else count)
    struct.pack_into(">I", hdr, 20, audio_rate)
    struct.pack_into(">I", hdr, 32, len(payload))
    path.write_bytes(hdr + payload)


class MovieSignatureTests(unittest.TestCase):
    def test_two_packets_with_audio_padding_and_known_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.MOV"
            video0 = bytes(range(1, 11))
            video1 = bytes(range(20, 32))
            audio1 = bytes(range(256)) * 4 + bytes(range(8))
            movie(path, [packet(video0), packet(video1, audio1)])
            self.assertEqual(signature(path), (2, 223642072))

    def test_truncated_packet_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.MOV"
            movie(path, [packet(bytes(range(10)))])
            content = bytearray(path.read_bytes())
            struct.pack_into(">I", content, 2048, 500)  # bogus video length
            path.write_bytes(content)
            with self.assertRaisesRegex(ValueError, "truncated packet"):
                signature(path)

    def test_packet_count_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.MOV"
            movie(path, [packet(bytes(range(10)))], count=2)
            with self.assertRaisesRegex(ValueError, "truncated packet header"):
                signature(path)

    def test_no_sound_rejects_audio(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.MOV"
            movie(path, [packet(bytes(range(10)), bytes(1032))], audio_rate=0)
            with self.assertRaisesRegex(ValueError, "invalid packet"):
                signature(path)


if __name__ == "__main__":
    unittest.main()
