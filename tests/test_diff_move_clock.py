"""Native clock versus pinned independent fight_sim._advance, synthetic RAM only."""
import argparse
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

RUNNER=UPSTREAM=None


class ClockDifferentialTests(unittest.TestCase):
    def test_original_clock_conditions_and_signed_wrap(self):
        if RUNNER is None or UPSTREAM is None:
            self.skipTest('pass --runner and --upstream for compiled clock comparison')
        sys.path.insert(0,str(UPSTREAM.parent))
        import fight_sim as source
        rng=random.Random(31998)
        cases,expected=[],[]
        ram=source.Ram(bytes(0x200000));fighter=source.Fighter(ram,0x80010000)
        names=('poseFrame','rootFrame','moveFrame','frameStep','hitFreeze',
               'holdFrames','ballistic','moveChanged')
        for _ in range(10000):
            vals=(rng.randrange(0,256),rng.randrange(0,256),
                  rng.choice([-32768,32767,rng.randrange(0,500)]),
                  rng.choice([-1,0,1,2]),rng.choice([-1,0,1,2,4]),
                  rng.choice([-1,0,1,2,7]),rng.randrange(2),rng.randrange(2),rng.randrange(256))
            for name,value in zip(names,vals):setattr(fighter,name,value)
            fighter.poseMove=0x80020000;fighter.rootMove=0x80020100
            ram.put(0x8002001B,'B',vals[8])
            source._advance(fighter)
            cases.append(vals)
            expected.append(tuple(getattr(fighter,name) for name in names)+
                            (int(fighter.rootMove==fighter.poseMove),))
        with tempfile.TemporaryDirectory() as tmp:
            inp,out=Path(tmp)/'input',Path(tmp)/'output'
            inp.write_bytes(b''.join(struct.pack('<9i',*v) for v in cases))
            subprocess.run([str(RUNNER),str(inp),str(out)],check=True)
            actual=list(struct.iter_unpack('<9i',out.read_bytes()))
        self.assertEqual(len(actual),len(cases))
        for i,(a,b) in enumerate(zip(actual,expected)):
            self.assertEqual(a,b,f'case {i}: {cases[i]}')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True)
    p.add_argument('--upstream',type=Path,required=True)
    args,rest=p.parse_known_args();RUNNER=args.runner.resolve();UPSTREAM=args.upstream.resolve()
    unittest.main(argv=[__file__]+rest)
