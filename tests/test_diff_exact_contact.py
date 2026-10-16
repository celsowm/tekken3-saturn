"""Compare exact C collision against pinned original arithmetic, no game data."""
import argparse
from pathlib import Path
import random
import subprocess
import sys
import unittest
RUNNER=UPSTREAM=None


class ExactContactDiff(unittest.TestCase):
    def test_original_wrapping_and_clipping(self):
        if RUNNER is None or UPSTREAM is None:self.skipTest('pass native runner and pinned research')
        sys.path.insert(0,str(UPSTREAM.parent))
        import fight_math as original
        rng=random.Random(3199803)
        cases=[[-1000,0,0,1000,0,0,0,0,0,10]]
        for _ in range(10000):
            center=[rng.randint(-2000,2000) for _ in range(3)]
            span=rng.choice([20,200,1000,5000,50000])
            points=[center[i%3]+rng.randint(-span,span) for i in range(6)]
            cases.append(points+center+[rng.randrange(501)])
        text='\n'.join(' '.join(map(str,case)) for case in cases)+'\n'
        result=subprocess.run([str(RUNNER)],input=text,text=True,capture_output=True,check=True)
        actual=[int(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(actual),len(cases))
        different_from_exact=0
        for index,(case,observed) in enumerate(zip(cases,actual)):
            cylinder=case[6:]+[case[9]*case[9]]
            expected=original.segment_hits_cylinder(case[:6],cylinder)
            self.assertEqual(observed,int(expected),f'case {index}: {case}')
            different_from_exact+=expected!=original.segment_hits_cylinder(case[:6],cylinder,exact=True)
        self.assertGreater(different_from_exact,0,'must exercise original overflow, not only ideal geometry')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True)
    p.add_argument('--upstream',type=Path,required=True)
    args,rest=p.parse_known_args();RUNNER=args.runner.resolve();UPSTREAM=args.upstream.resolve()
    unittest.main(argv=[__file__]+rest)
