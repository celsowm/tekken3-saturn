import sys
import struct
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import extract_psx_sfx as sfx
import inspect_psx_sound_ids as inspect_sounds


class Tests(unittest.TestCase):
    def test_effect_request_resolves_low_twelve_bits(self):
        exe = bytearray(sfx.SOUND_TABLE_OFFSET + 2 * sfx.SOUND_TABLE_COUNT)
        struct.pack_into('<H', exe, sfx.SOUND_TABLE_OFFSET + 0x4f * 2, 0x6941)
        self.assertEqual(sfx.effect_code(exe, 0x104f), 0x6941)
        self.assertEqual(sfx.effect_code(exe, 0x704f), 0x6941)
        with self.assertRaises(ValueError):
            sfx.effect_code(exe, 0x004f)
        with self.assertRaises(ValueError):
            sfx.effect_code(exe, 0x1fff)
        with self.assertRaises(ValueError):
            sfx.effect_code(exe, 0x1000)
        struct.pack_into('<H', exe, sfx.SOUND_TABLE_OFFSET + 0x3b * 2, 0x870d)
        self.assertEqual(sfx.system_code(exe, 0x3b), 0x870d)
        with self.assertRaises(ValueError):
            sfx.system_code(exe, 0x4f)
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

    def test_inspector_rejects_unsupported_executable(self):
        with self.assertRaisesRegex(ValueError, 'unsupported USA executable'):
            inspect_sounds.inspect(b'not an executable', 'effect', [0x1053])


EXE = Path(__file__).resolve().parents[1] / 'assets/imported/tekken3-usa/SLUS_004.02'


@unittest.skipUnless(EXE.is_file(), 'private USA executable not imported')
class Private(unittest.TestCase):
    def test_paul_and_law_voice_codes(self):
        d = EXE.read_bytes()
        self.assertEqual(sfx.voice_codes(d, 0),
                         ([0xD944, 0xD946, 0xD947, 0xD948, 0xD950, 0xD951],
                          [0xD941, 0xD942, 0xD943], [0xD949, 0xD94A, 0xD94B]))
        self.assertEqual(sfx.voice_codes(d, 1),
                         ([0xD944, 0xD945, 0xD946, 0xD8C7, 0xD948, 0xD8D1],
                          [0xD981, 0xD982, 0xD943], [0xD8C9, 0xD8CA, 0xD8CB]))

    def test_effect_ids_are_resolved_before_vab_tone_selection(self):
        d = EXE.read_bytes()
        for request, code in ((0x104f, 0x6941), (0x1053, 0x6845),
                              (0x105a, 0x68cd), (0x704d, 0x60c3)):
            self.assertEqual(sfx.effect_code(d, request), code)
        self.assertEqual(sfx.system_code(d, 0x3b), 0x870d)
        rows = inspect_sounds.inspect(d, 'effect', [0x105a])
        self.assertEqual(rows[0]['code'], '0x68CD')
        self.assertGreater(rows[0]['samples'], 0)


if __name__ == '__main__':
    unittest.main()
