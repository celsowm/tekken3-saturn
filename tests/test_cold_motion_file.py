"""MOTION.BIN layout for T3_COLD_LOW_WRAM; synthetic bytes, no retail data."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import build_local_divmot_assets as assets


class ColdMotionFileTests(unittest.TestCase):
    def test_fnv1a_reference_vectors(self):
        self.assertEqual(assets.fnv1a(b''),0x811C9DC5)
        self.assertEqual(assets.fnv1a(b'a'),0xE40C292C)
        self.assertEqual(assets.fnv1a(b'b',assets.fnv1a(b'a')),assets.fnv1a(b'ab'))

    def test_layout_aligns_banks_and_records_checksum(self):
        bank=bytes(range(1,250))*3
        common=bytes(range(7,200))
        image,bank_at,common_at,second_at,digest=assets.cold_file(bank,common)
        self.assertEqual(second_at,0)
        self.assertEqual(bank_at,64)
        self.assertEqual(common_at%16,0)
        self.assertGreaterEqual(common_at,bank_at+len(bank))
        self.assertEqual(image[bank_at:bank_at+len(bank)],bank)
        self.assertEqual(image[common_at:],common)
        self.assertEqual(set(image[bank_at+len(bank):common_at]),{0} if common_at>bank_at+len(bank) else set())
        self.assertEqual(struct.unpack_from('>7I',image),
                         (0x54334D4F,1,len(bank),len(common),bank_at,common_at,digest))
        self.assertEqual(set(image[28:64]),{0})
        self.assertEqual(digest,assets.fnv1a(bank+common))

    def test_already_aligned_bank_leaves_no_gap(self):
        bank=b'\x11'*32
        image,_,common_at,_,_=assets.cold_file(bank,b'\x22')
        self.assertEqual(common_at,96)
        self.assertEqual(len(image),97)

    def test_version_2_appends_the_second_bank(self):
        bank=bytes(range(1,250))*3
        common=bytes(range(7,200))
        second=bytes(range(3,90))*2
        image,bank_at,common_at,second_at,digest=assets.cold_file(bank,common,second)
        self.assertEqual(second_at%16,0)
        self.assertGreaterEqual(second_at,common_at+len(common))
        self.assertEqual(image[second_at:],second)
        self.assertEqual(image[common_at:common_at+len(common)],common)
        self.assertEqual(struct.unpack_from('>9I',image),
                         (0x54334D4F,2,len(bank),len(common),bank_at,common_at,digest,
                          len(second),second_at))
        self.assertEqual(set(image[36:64]),{0})
        self.assertEqual(digest,assets.fnv1a(bank+common+second))


if __name__=='__main__':
    unittest.main()
