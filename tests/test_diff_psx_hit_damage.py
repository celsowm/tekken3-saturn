"""Host C HitClassify/HitDamage vs independently maintained PS1 fight_math.py.

Synthetic-only CI: no retail table bytes, executable, captures or extracted
move banks. Randomized classifications use the documented combat contracts;
the damage calculation itself is checked against the pinned independent
Tekken3Remake integer port.
"""
import argparse
import importlib.util
import random
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

INPUT=struct.Struct("<21i")
OUTPUT=struct.Struct("<13i")
EVALUATOR=None
UPSTREAM=None

def reference(source):
    name="t3_upstream_fight_math_test"
    spec=importlib.util.spec_from_file_location(name,source)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot import independent research oracle")
    module=importlib.util.module_from_spec(spec)
    sys.modules[name]=module
    spec.loader.exec_module(module)
    return module

def cases():
    rng=random.Random(19031998)
    result=[]
    for zone in range(14):
        for base in (0,7,57,101,350,-35):
            for variation in (0,1,2,3,4,5):
                rec=zone*7+9
                close=1 if variation in (3,5) else 0
                stance=rng.choice((0,0x8,0x10,0x18))
                angle=rng.choice((0,0x3FFF,0x4000,0x6000,0xFFFF))
                pose=rng.randrange(0,20)
                active=rng.randrange(1,24)
                defender_damage=rng.choice((-100,0,40,120,400))
                power_a=int(variation==5)
                power_d=int(variation==4)
                juggle=rng.choice((0,0,1,3,8))
                airborne=int(variation==2)
                chip=int(variation==1)
                distance=rng.randrange(100,1200)
                threshold=600
                row=(zone,rng.choice((0x217,0x412,0x10F,0x607)),
                     rng.randrange(0,4)*0x4000+(abs(base)&0x3fff),
                     rec,close,base,defender_damage,stance,angle,
                     pose,active,rng.choice((0,0x400)),juggle,
                     distance,threshold,0x1004,1,power_a,
                     power_d,chip,airborne)
                result.append(row)
    return result

def classify(vals):
    (zone,attack,cls,rec,close_idx,base,defd,guard,angle,
     frame,active,state,juggle,distance,threshold,close_word,
     present,power_a,power_d,chip_enabled,in_air)=vals
    close=int(bool(close_idx) and distance<threshold)
    guarded=int(bool(guard & attack) and angle<=0x3fff)
    hit=1-guarded
    chip=int(bool(guarded and (chip_enabled or power_a)))
    counter=int(bool(hit and ((defd!=0 and frame<active) or power_a or power_d)))
    airborne=int(bool(hit and (in_air or (state&0x400))))
    severity=int(base==0)+2*guarded+3*chip+4*close+5*hit+6*counter+7*airborne
    raw=close_word if close else rec
    kind=0 if raw<0x1000 else (1 if raw<0x2000 else 2)
    return (guarded,chip,close,hit,counter,airborne,severity,raw,kind,cls>>14)

class Differential(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if EVALUATOR is None or not EVALUATOR.is_file():
            raise AssertionError("C hit damage evaluator missing")
        if UPSTREAM is None or not UPSTREAM.is_file():
            raise AssertionError("pinned research fight_math.py missing")
        cls.source=reference(UPSTREAM)

    def test_per_zone_randomized_parity(self):
        entries=cases()
        with tempfile.TemporaryDirectory(prefix="t3-hit-diff-") as temp:
            root=Path(temp)
            (root/"input.bin").write_bytes(b"".join(INPUT.pack(*r) for r in entries))
            p=subprocess.run([str(EVALUATOR),(root/"input.bin").as_posix(),
                              (root/"output.bin").as_posix()],
                             capture_output=True,text=True,check=False)
            self.assertEqual(p.returncode,0,p.stderr)
            data=(root/"output.bin").read_bytes()
        self.assertEqual(len(data),OUTPUT.size*len(entries))
        for i,row in enumerate(entries):
            expected_flags=classify(row)
            zone,_,_,_,_,base,defd,_,_,pose,active,_,juggle,_,_,_,_,pa,pd,_,_=row
            guard,chip,close,hit,counter,air,severity,reaction,kind,aclass=expected_flags
            ref=self.source.hit_damage(
                {"damage":defd,"powerTimer":pd,"moveActiveStart":active,
                 "poseFrame":pose,"juggleCount":juggle},
                {"damage":base,"powerTimer":pa},
                {"zone":zone,"guarded":guard,"chip":chip,"close":close,
                 "counter":counter,"airborne":air})
            actual=OUTPUT.unpack_from(data,i*OUTPUT.size)
            wanted=(ref,base,int(base==0),guard,chip,close,hit,
                    counter,air,severity,reaction,kind,aclass)
            self.assertEqual(actual,wanted,f"input #{i}: {row}")
    def test_bad_binary_fails_without_output(self):
        with tempfile.TemporaryDirectory(prefix="t3-hit-invalid-") as temp:
            root=Path(temp)
            for bad in (b"",b"\0"*15,INPUT.pack(14,*([0]*20))):
                (root/"input.bin").write_bytes(bad)
                p=subprocess.run([str(EVALUATOR),(root/"input.bin").as_posix(),
                                  (root/"output.bin").as_posix()],
                                 capture_output=True,text=True,check=False)
                self.assertNotEqual(p.returncode,0)
                self.assertFalse((root/"output.bin").exists())

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--evaluator",type=Path,required=True)
    parser.add_argument("--upstream",type=Path,required=True)
    a=parser.parse_args()
    EVALUATOR=a.evaluator.resolve()
    UPSTREAM=a.upstream.resolve()
    unittest.main(argv=[sys.argv[0]],verbosity=2)
