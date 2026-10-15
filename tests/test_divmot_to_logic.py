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

    def test_keep_section_9_attack_points_without_other_bank_sections(self):
        source=fake_bank(99)
        attack=bytes(range(80))
        # Fake original: the 32-byte section3 is discarded; section9 is
        # retained only when explicitly opting into authored hitbox data.
        data=bytearray(source+attack)
        for i in range(5,10):
            struct.pack_into("<I",data,4*i+4,len(source))
        for i in range(10,15):
            struct.pack_into("<I",data,4*i+4,len(data))
        default,default_meta=t3.compact(bytes(data))
        self.assertFalse(default_meta["attack_geometry_included"])
        with_points,meta=t3.compact(bytes(data),with_hitboxes=True)
        self.assertEqual(meta["hitbox_bytes"],len(attack))
        self.assertEqual(t3.sections_of(with_points)[1][9],
                         t3.sections_of(default)[1][-1])
        self.assertEqual(with_points[-len(attack):],attack)
        self.assertEqual(len(with_points)-len(default),len(attack))
        self.assertEqual(t3.compact(with_points,with_hitboxes=True)[0],with_points)
        self.assertEqual(t3.compact(with_points)[0],default)

    def test_reject_malformed(self):
        source=bytearray(fake_bank())
        source[0]=7
        with self.assertRaises(t3.FormatError):t3.compact(bytes(source))
        source=bytearray(fake_bank())
        struct.pack_into("<I",source,12,33)
        with self.assertRaises(t3.FormatError):t3.compact(bytes(source))

if __name__=="__main__":unittest.main()
