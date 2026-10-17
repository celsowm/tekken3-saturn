"""Public input-adapter checks; no original snapshots or retail fixtures."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from audit_psx_match_replay import words,mapped_pad_words,frame_pad_words

class InputWords(unittest.TestCase):
    def test_gameplay_reads_late_mapped_words_and_replay_allows_no_source(self):
        early={'pads':dict(held0=0x80,held1=0,pressed0=0,pressed1=0)}
        left={'pads':dict(held0=0,held1=0,pressed0=0,pressed1=0)}
        right={'pads':dict(held0=0,held1=0x8000,pressed0=0,pressed1=0)}
        self.assertEqual(frame_pad_words(early,{0:left,1:right},[0,1],7),(0,0,1,0))
        self.assertEqual(frame_pad_words(early,{},[0,1],1),(32,0,0,0))
        with self.assertRaisesRegex(ValueError,'missing live'):
            frame_pad_words(early,{},[0,1],7)
    def test_mapped_pads_mirror_horizontal_and_neutralize_opposites(self):
        self.assertEqual(mapped_pad_words(0x2000,0,0),(1,0))
        self.assertEqual(mapped_pad_words(0x2000,0,1),(2,0))
        self.assertEqual(mapped_pad_words(0x8000,0,1),(1,0))
        self.assertEqual(mapped_pad_words(0xF000,0,0),(0,0))
        self.assertEqual(mapped_pad_words(0x6080,0x80,0),(1|8|32,32))
        self.assertEqual(mapped_pad_words(0xF0,0x50,1),(32|64|128|256,64|128))
        with self.assertRaises(ValueError):mapped_pad_words(0,0,2)
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
