import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import extract_psx_sfx as sfx


class Tests(unittest.TestCase):
    def test_rate_follows_the_documented_pitch_rule(self):
        # center - key = 12 (one octave down), 24 and 18 with tuning 70
        self.assertEqual(sfx.rate(72, 0, 60), 22050)
        self.assertEqual(sfx.rate(84, 0, 60), 11025)
        self.assertEqual(sfx.rate(78, 70, 60), 16042)

    def test_decode_drops_the_lead_in_and_stops_at_the_end_block(self):
        silent = bytes(16)
        loud = bytes([0x00, 0x00]) + bytes([0x11] * 14)   # filter 0, shift 0: every sample 4096
        end = bytes([0x00, 0x07]) + b'\x77' * 14
        self.assertEqual(sfx.decode(silent + loud + end + loud), [4096] * 28)

    def test_decode_filter_one_carries_history(self):
        silent = bytes(16)
        frame = bytes([0x10, 0x00, 0x01]) + bytes(13)       # filter 1, shift 0, first nibble 1
        # 4096, then each sample is 60/64 of the one before
        self.assertEqual(sfx.decode(silent + frame)[:3], [4096, 3840, 3600])


EXE = Path(__file__).resolve().parents[1] / 'assets/imported/tekken3-usa/SLUS_004.02'


@unittest.skipUnless(EXE.is_file(), 'private USA executable not imported')
class Private(unittest.TestCase):
    def test_paul_and_law_voice_codes(self):
        d = EXE.read_bytes()
        self.assertEqual(sfx.voice_codes(d, 0), ([0xD944, 0xD946], [0xD941, 0xD942, 0xD943], [0xD949]))
        self.assertEqual(sfx.voice_codes(d, 1), ([0xD944, 0xD945], [0xD981, 0xD982, 0xD943], [0xD8C9]))


if __name__ == '__main__':
    unittest.main()
