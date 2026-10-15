"""Original FK/IK table exporter: synthetic PS-X executable only."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from extract_psx_ik_tables import normalize,TableError

OFFSETS={"sine":0x1000,"sqrt":0x4000,"basis":0x4200,
         "skeleton":0x4300,"limb":0x4400}
def fake():
    data=bytearray(0x5000)
    data[:8]=b"PS-X EXE"
    struct.pack_into("<I",data,0x18,0x80010000)
    struct.pack_into("<h",data,0x1800,4096)  # cos(0)
    for i in range(192):
        struct.pack_into("<h",data,0x4000+i*2,6400)
    for i in range(3):
        struct.pack_into("<h",data,0x4200+2*(i*3+i),4096)
    return bytes(data)

class Tests(unittest.TestCase):
    def test_parse_known_regions_and_sizes(self):
        exe=fake()
        trig,ik,manifest=normalize(exe,OFFSETS,expected_sha=None)
        self.assertEqual(len(trig),0x2800)
        self.assertEqual(len(ik),0x1F2)
        self.assertEqual(struct.unpack_from("<h",ik,0)[0],6400)
        self.assertEqual(struct.unpack_from("<h",ik,0x180+2*4)[0],4096)
        self.assertEqual(manifest["source_sha256"],hashlib.sha256(exe).hexdigest())
        self.assertEqual(manifest["ik_sha256"],hashlib.sha256(ik).hexdigest())
        self.assertIn("PRIVATE",manifest["visibility"])
    def test_bad_revision_and_offsets(self):
        exe=fake()
        with self.assertRaises(TableError):
            normalize(exe,OFFSETS)
        for field,offset in (("sine",0x1),("sqrt",0x5000),
                             ("basis",0x4000),("limb",0x1000)):
            bad=dict(OFFSETS);bad[field]=offset
            with self.subTest(field=field),self.assertRaises(TableError):
                normalize(exe,bad,expected_sha=None)
    def test_reject_false_table_matches(self):
        src=bytearray(fake())
        src[0x1800:0x1802]=b"\x00\x00"
        with self.assertRaises(TableError):
            normalize(bytes(src),OFFSETS,None)
        src=bytearray(fake())
        src[0x4000:0x4002]=b"\x00\x00"
        with self.assertRaises(TableError):
            normalize(bytes(src),OFFSETS,None)
        src=bytearray(fake())
        src[0x4200:0x4202]=b"\xff\x7f"
        with self.assertRaises(TableError):
            normalize(bytes(src),OFFSETS,None)

if __name__=="__main__":unittest.main()
