"""Synthetic RAM integration of the native USA HitApply adapter."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

RUNNER=None


class HitRamAdapterTests(unittest.TestCase):
    def fixture(self):
        ram=bytearray(2*1024*1024)
        def put(address,fmt,value):struct.pack_into('<'+fmt,ram,address-0x80000000,value)
        a,b,arow,brow=0x800A9228,0x800AAAB4,0x80080000,0x80080100
        put(0x800AFA88,'I',1) # USA VS mode
        put(a+0x54,'I',arow);put(b+0x54,'I',brow)
        put(arow+8,'H',0x412);put(arow+0x14,'H',5);put(arow+0x32,'H',1)
        put(a+0x5E,'h',5);put(b+0x60,'I',2);put(brow+4,'I',2)
        put(b+0x3F4,'i',100*65536);put(b+0x3F8,'i',140*65536)
        put(b+0xCE,'B',1);put(b+0x58,'h',5)
        slot=b+0x13C+0x2C
        put(slot+0x21,'B',1);put(slot+0x18,'h',6)
        put(0x80010CE8+42,'h',2250)
        return ram,put

    def run_adapter(self,ram):
        if RUNNER is None:self.skipTest('pass --runner to test compiled RAM adapter')
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'synthetic.bin';path.write_bytes(ram)
            return subprocess.run([str(RUNNER),str(path),'1'],capture_output=True,text=True)

    def test_raw_snapshot_drives_original_damage_and_reaction(self):
        ram,_=self.fixture()
        r=self.run_adapter(ram)
        self.assertEqual(r.returncode,0,r.stderr)
        result=json.loads(r.stdout)
        self.assertEqual(result['health_q16'],94*65536)
        self.assertEqual(result['reaction_slot'],2250)
        self.assertEqual(result['slots'][1]['damage'],6)

    def test_original_no_damage_flag_preserves_health(self):
        ram,put=self.fixture();put(0x8009547C,'I',1)
        result=json.loads(self.run_adapter(ram).stdout)
        self.assertEqual(result['health_q16'],100*65536)
        self.assertEqual(result['total_damage'],6)

    def test_rejects_invalid_guest_pointer_and_wrong_mode(self):
        ram,put=self.fixture();put(0x800A9228+0x54,'I',0x80200000)
        self.assertNotEqual(self.run_adapter(ram).returncode,0)
        ram,put=self.fixture();put(0x800AFA88,'I',8)
        self.assertNotEqual(self.run_adapter(ram).returncode,0)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True)
    args,rest=p.parse_known_args();RUNNER=args.runner.resolve()
    unittest.main(argv=[__file__]+rest)
