"""Independent upstream Tekken3Remake Python-vs-C 18-slot pose parity.

The CI checkout initializes an EXISTING pinned research submodule; all
lookup tables and pose samples here are synthetic and redistributable.
No commercial game byte is necessary.
"""
import argparse
import math
from pathlib import Path
import random
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
import diff_psx_pose_solver as diff

RUNNER=None
UPSTREAM=None

def synthetic_tables():
    trig=bytearray(0x2800)
    for i in range(len(trig)//2):
        # PS1 index (angle >> 3) & 0x1FFE, 16 angle units per entry.
        value=round(4096*math.sin(2*math.pi*i/4096))
        struct.pack_into("<h",trig,2*i,value)
    ik=bytearray(0x1F2)
    for i in range(192):
        # Deliberately synthetic integer mantissa, not the Namco table.
        value=math.isqrt((i+0x40)<<18)
        struct.pack_into("<h",ik,2*i,value)
    for i in range(3):
        struct.pack_into("<h",ik,0x180+2*(i*3+i),4096)
    return bytes(trig),bytes(ik)

def sample_poses():
    out=[]
    def sample(arm,leg,angles=()):
        p=[0]*49
        p[3]=arm;p[6]=arm
        p[9]=leg;p[12]=leg
        for i,val in enumerate(angles[:30]):
            p[19+i]=val
        out.append(p)
    sample(600,960)
    sample(300,400)
    sample(0,400)
    sample(80,6)
    sample(1000,1600)
    sample(480,820,angles=(0,16384,0, 0,-16384,0))
    rng=random.Random(150326)
    for i in range(10):
        arm=rng.randrange(220,700)
        leg=rng.randrange(320,920)
        angles=[rng.randrange(-2048,2049) for _ in range(30)]
        sample(arm,leg,angles)
    return b"".join(diff.POSE.pack(*p) for p in out)

class Compare(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not RUNNER or not RUNNER.is_file():
            raise AssertionError("native C pose evaluator missing; build t3_pose_eval first")
        if not UPSTREAM or not UPSTREAM.is_file():
            raise AssertionError("pinned original-research submodule absent; run "
                                 "git submodule update --init third_party/Tekken3Remake")
    def test_reference_parity_all_valid_components(self):
        trig,ik=synthetic_tables()
        report=diff.compare(RUNNER,UPSTREAM,trig,ik,sample_poses())
        self.assertEqual(report["frames"],16)
        self.assertGreater(report["components_compared"],1000)
        self.assertEqual(report["mismatch_count"],0,report["first_differences"])
    def test_corrupt_input_fails_closed(self):
        trig,ik=synthetic_tables()
        samples=sample_poses()
        with self.assertRaises(diff.DifferentialError):
            diff.compare(RUNNER,UPSTREAM,trig,ik,samples[:-1])
        with self.assertRaises(diff.DifferentialError):
            diff.compare(RUNNER,UPSTREAM,trig[:-1],ik,samples)
        with self.assertRaises(diff.DifferentialError):
            diff.compare(RUNNER,UPSTREAM,trig,ik[:-1],samples)
        with self.assertRaises(diff.DifferentialError):
            diff.compare(RUNNER,UPSTREAM,trig,ik,b"")

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--runner",type=Path,required=True)
    parser.add_argument("--upstream",type=Path,required=True)
    args=parser.parse_args()
    RUNNER=args.runner.resolve()
    UPSTREAM=args.upstream.resolve()
    unittest.main(argv=[sys.argv[0]],verbosity=2)
