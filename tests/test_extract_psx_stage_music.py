import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import extract_psx_stage_music as music


class StageMusicTests(unittest.TestCase):
    def test_refuses_output_outside_private_import(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, 'private assets/imported'):
                music.extract(Path('unused.cue'), Path(tmp) / 'output', 7, 0, 'ffmpeg')

    def test_rejects_invalid_track_before_opening_disc(self):
        out = music.ROOT / 'assets/imported/unused-stage-music-test'
        with self.assertRaisesRegex(ValueError, 'track must be'):
            music.extract(Path('unused.cue'), out, 32, 0, 'ffmpeg')


if __name__ == '__main__':
    unittest.main()
