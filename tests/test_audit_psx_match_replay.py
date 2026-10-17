"""Public input-adapter checks; no original snapshots or retail fixtures."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from audit_psx_match_replay import words

class InputWords(unittest.TestCase):
    def test_clear_is_distinct_from_neutral(self):
        self.assertEqual(words(dict(direction=5,held=0,pressed=0)),(0x8000,0))
        self.assertEqual(words(dict(direction=1<<9,held=0,pressed=0)),(0,0))

    def test_normal_buttons_and_direction(self):
        self.assertEqual(words(dict(direction=1<<10,held=1,pressed=1)),(1|32,32))

    def test_unrecognized_clear_or_direction_is_rejected(self):
        for direction,held,pressed in ((5,1,0),(5,0,1),(3,0,0),(1<<9,16,0)):
            with self.subTest(direction=direction,held=held,pressed=pressed):
                with self.assertRaises(ValueError):words(dict(direction=direction,held=held,pressed=pressed))

if __name__=='__main__':unittest.main()
