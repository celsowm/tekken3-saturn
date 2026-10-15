"""Synthetic PS-X EXE, 104-pointer table and malformed sequence tests."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
import extract_psx_sequences as seq

def make_exe():
    base=0x80010000
    exe=bytearray(0x4800)
    exe[:8]=b"PS-X EXE"
    struct.pack_into("<I",exe,0x18,base)
    table=0x1000
    for i in range(seq.N):
        offset=0x3000+i*16
        struct.pack_into("<I",exe,table+4*i,base+offset-seq.EXE_HEADER)
        struct.pack_into("<hHHHH",exe,offset,25,2,3,0x106,0)
    return bytes(exe)

class TestSequences(unittest.TestCase):
    def test_autodiscovery_both_tables(self):
        exe=make_exe()
        report=seq.extract(exe,expected_sha=None)
        self.assertEqual(report["table_file_offset"],0x1000)
        self.assertEqual(report["count"],104)
        self.assertEqual(report["sequences"][0]["command"],0xC00E)
        self.assertEqual(report["sequences"][62]["command"],0xC04C)
        self.assertEqual(report["sequences"][63]["command"],0xC7FF)
        self.assertEqual(report["sequences"][-1]["command"],0xC827)
        self.assertEqual(report["sequences"][-1]["steps"],[2,3,0x106])
        self.assertEqual(report["exe_sha256"],hashlib.sha256(exe).hexdigest())
        self.assertEqual(seq.extract(exe,0x1000,None),report)

    def test_revision_and_malformed_pointer(self):
        exe=make_exe()
        with self.assertRaises(seq.SequenceError):
            seq.extract(exe,expected_sha="deadbeef")
        bad=bytearray(exe)
        struct.pack_into("<I",bad,0x1000+4*90,0xFEEDBEEF)
        with self.assertRaises(seq.SequenceError):
            seq.at_table(bytes(bad),0x80010000,0x1000)
        with self.assertRaises(seq.SequenceError):
            seq.detect_table(bytes(bad))
        with self.assertRaises(seq.SequenceError):
            seq.at_table(exe,0x80010000,0x1001)

    def test_sequence_limit_and_bad_spec(self):
        exe=bytearray(make_exe())
        # The first sequence contains an impossible direction nibble 0xF.
        off=0x3000
        struct.pack_into("<H",exe,off+2,0x00FF)
        with self.assertRaises(seq.SequenceError):
            seq.read_sequence(bytes(exe),0x80010000+off-0x800,0x80010000)
        struct.pack_into("<H",exe,off+2,2)
        struct.pack_into("<h",exe,off,255)
        with self.assertRaises(seq.SequenceError):
            seq.read_sequence(bytes(exe),0x80010000+off-0x800,0x80010000)

if __name__=="__main__":
    unittest.main()
