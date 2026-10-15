"""No original PlayStation files: fabricate DIVMOT bank and test the compactor."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
import divmot_to_logic as t3

def fake_bank(kind=0):
    b0=64
    b1=b0+12
    b2=b1+56
    b3=b2+(0 if kind==99 else t3.SLOTS*4)
    b4=b3+32
    buf=bytearray(b4)
    buf[1]=kind
    struct.pack_into("<H",buf,2,1)
    sections=(b0,b1,b2,b3)+tuple([b4]*11)
    struct.pack_into("<15I",buf,4,*sections)
    struct.pack_into("<H",buf,b0,0xC000)
    buf[b1+24]=16
    if kind!=99:
        for s in range(t3.SLOTS):
            struct.pack_into("<I",buf,b2+s*4,0 if s==0 else 0x3FFFFFFF)
    buf[b3:b4]=bytes(range(32))
    return bytes(buf)

class Tests(unittest.TestCase):
    def test_bank_compact(self):
        source=fake_bank(0)
        out,meta=t3.compact(source)
        self.assertLess(len(out),len(source))
        self.assertEqual(len(source)-len(out),32)
        self.assertEqual(meta["move_rows"],1)
        self.assertEqual(meta["branch_rows"],1)
        self.assertEqual(meta["slot_count"],4023)
        self.assertEqual(out[64:],source[64:-32])
        self.assertEqual(t3.sections_of(out)[1][-1],len(out))
        self.assertEqual(t3.compact(out)[0],out)

    def test_common_compact(self):
        source=fake_bank(99)
        out,meta=t3.compact(source)
        self.assertEqual(meta["bank_type"],99)
        self.assertEqual(meta["slot_count"],0)
        self.assertEqual(t3.sections_of(out)[0],1)

    def test_reject_malformed(self):
        source=bytearray(fake_bank())
        source[0]=7
        with self.assertRaises(t3.FormatError):t3.compact(bytes(source))
        source=bytearray(fake_bank())
        struct.pack_into("<I",source,12,33)
        with self.assertRaises(t3.FormatError):t3.compact(bytes(source))

if __name__=="__main__":unittest.main()
