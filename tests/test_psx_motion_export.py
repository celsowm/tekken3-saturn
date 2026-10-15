"""Private DIVMOT -> raw 49-channel pose sample exporter.

Only synthetic DIVMOT banks; verifies bounds, common-bank dispatch and
failure without leaking partial/stale pose data.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tests"))
from test_divmot_to_logic import fake_bank

RUNNER=None

def animated_bank(kind=0):
    data=bytearray(fake_bank(kind))
    start=len(data)
    hdr=bytearray(2+50)
    struct.pack_into("<H",hdr,0,0x1234) # s16 tag before header
    hdr[2]=4 # frames
    hdr[3]=4 # channel0: four exact samples
    hdr[4]=1 # channel1: constant reverse-time
    data+=hdr
    for value in (10,20,30,40,-12):
        data+=struct.pack("<h",value)
    # section6 at stream tag, section7..14 at EOF
    for section in range(6,15):
        struct.pack_into("<I",data,4+4*section,
                         start if section==6 else len(data))
    struct.pack_into("<I",data,76,2) # animation offset relative to section6
    return bytes(data)

def sample_out(cmd,character,common=None,slot=0,start=0,count=0,
               spline=None):
    with tempfile.TemporaryDirectory(prefix="t3-private-motion-") as tmp:
        tmp=Path(tmp)
        (tmp/"char.bin").write_bytes(character)
        if common is not None:
            (tmp/"common.bin").write_bytes(common)
        if spline is not None:
            (tmp/"splines.bin").write_bytes(spline)
        out=tmp/"poses.bin"
        call=[str(cmd.resolve()),str(tmp/"char.bin"),
              str(tmp/"common.bin") if common is not None else "-",
              str(tmp/"splines.bin") if spline is not None else "-",
              str(slot),str(start),str(count),str(out)]
        process=subprocess.run(call,capture_output=True,text=True,check=False)
        return process,out.read_bytes() if out.exists() else None

class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not RUNNER or not RUNNER.is_file():
            raise AssertionError("t3_motion_export was not built")
    def test_local_pose_samples_and_frame_window(self):
        process,data=sample_out(RUNNER,animated_bank())
        self.assertEqual(process.returncode,0,process.stderr)
        self.assertEqual(len(data),4*98)
        for frame in range(4):
            p=struct.unpack_from("<49h",data,frame*98)
            self.assertEqual(p[:3],((frame+1)*20,-24,0))
            self.assertEqual(p[3:],(0,)*46)
        process,data=sample_out(RUNNER,animated_bank(),start=1,count=2)
        self.assertEqual(process.returncode,0,process.stderr)
        self.assertEqual(len(data),196)
        self.assertEqual(struct.unpack_from("<h",data)[0],40)
    def test_common_move_uses_common_animation_section(self):
        char=bytearray(fake_bank())
        struct.pack_into("<I",char,132,0x40000000)
        process,data=sample_out(RUNNER,bytes(char),animated_bank(99))
        self.assertEqual(process.returncode,0,process.stderr)
        self.assertEqual(struct.unpack_from("<h",data)[0],20)
        self.assertEqual(len(data),392)
    def test_missing_motion_or_splines_fails_without_output(self):
        process,data=sample_out(RUNNER,fake_bank())
        self.assertNotEqual(process.returncode,0)
        self.assertIsNone(data)
        process,data=sample_out(RUNNER,animated_bank(),start=4)
        self.assertNotEqual(process.returncode,0)
        self.assertIsNone(data)
        process,data=sample_out(RUNNER,animated_bank(),spline=b"\0")
        self.assertNotEqual(process.returncode,0)
        self.assertIsNone(data)
        process,data=sample_out(RUNNER,animated_bank(),slot=4000)
        self.assertNotEqual(process.returncode,0)
        self.assertIsNone(data)

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--runner",type=Path,required=True)
    args=parser.parse_args()
    RUNNER=args.runner.resolve()
    unittest.main(argv=[sys.argv[0]],verbosity=2)
