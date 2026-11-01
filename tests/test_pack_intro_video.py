"""scripts/pack_intro_video.py on synthetic data: no disc, no ffmpeg. The converter itself is tested in LibSaturn."""
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import pack_intro_video as pack  # noqa: E402


class Disc(unittest.TestCase):
    def test_the_audio_start_is_read_from_the_sector_order(self):
        raw = bytearray(pack.psx_movie.RAW * 12)
        for i in range(12):
            base = i * pack.psx_movie.RAW
            if i in (3, 11):
                raw[base + 18] = 0x64                       # a Form 2 audio sector
            elif i >= 1:
                struct.pack_into('<HH', raw, base + pack.psx_movie.USER, pack.psx_movie.STR_MAGIC, pack.psx_movie.STR_TYPE)
        self.assertAlmostEqual(pack.audio_start_seconds(bytes(raw)), (3 - 1) / 150)

    def test_the_converter_is_found(self):
        self.assertTrue(hasattr(pack.satmovie_module(), 'write_movie'))


if __name__ == '__main__':
    unittest.main()
