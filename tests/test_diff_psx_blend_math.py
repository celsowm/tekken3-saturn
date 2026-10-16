"""Exact blend arithmetic against pinned research; CI uses synthetic table bytes."""
import argparse
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

RUNNER = UPSTREAM = TABLE = None


class BlendMathDifferential(unittest.TestCase):
    def test_original_normalization_and_scaled_matrix(self):
        if RUNNER is None or UPSTREAM is None:
            self.skipTest('pass native runner and pinned research')
        sys.path.insert(0, str(UPSTREAM.parent))
        import fight_sim as original
        rng = random.Random(319980416)
        raw = TABLE.read_bytes() if TABLE else bytes(rng.randrange(256) for _ in range(768))
        self.assertEqual(len(raw), 768)

        class Exe:
            def u8(self, address):
                index = address - (original.RSQRT_TABLE - 384)
                if not 0 <= index < len(raw):
                    raise ValueError('reference addressed outside coefficient window')
                return raw[index]

        before = original.exe
        original.exe = lambda: Exe()
        try:
            edges = ([0, 0, 0], [1, 0, 0], [-1, 0, 0], [4096, 0, 0],
                     [32767]*3, [-32768]*3, [32767, -32768, 1],
                     [65536, -65537, 2147483647])
            cases, expected = [], []
            negative_lengths = saturated_sums = 0
            for i in range(10008):
                vector = list(edges[i]) if i < len(edges) else [rng.randrange(-65536, 65537) for _ in range(3)]
                bound = (0, 1, 4096, 32768)[i % 4]
                base = [rng.randrange(-bound, bound+1) if bound < 32768 else rng.randrange(-32768, 32768) for _ in range(9)]
                delta = [rng.randrange(-32768, 32768) for _ in range(9)]
                weight = rng.choice([-32768, -4096, -1, 0, 1, 4096, 32767, rng.randrange(-32768, 32768)])
                summed = original.matrix_add_scaled(delta, base, weight)
                expected.append(original.vector_normal(vector) + original.matrix_orthonormalize(base) +
                                summed + original.matrix_orthonormalize(summed))
                cases.append(vector + base + delta + [weight])
                negative_lengths += original.w32(sum(original.s16(x)**2 for x in vector)) < 0
                saturated_sums += any(abs((weight*d) >> 12) > 32767 for d in delta)
            with tempfile.TemporaryDirectory() as tmp:
                table = Path(tmp)/'rsqrt.bin'
                table.write_bytes(raw)
                result = subprocess.run([str(RUNNER), str(table)], text=True,
                                        input=''.join(' '.join(map(str, x))+'\n' for x in cases),
                                        capture_output=True, check=True)
            observed = [list(map(int, line.split())) for line in result.stdout.splitlines()]
            self.assertEqual(len(observed), len(expected))
            for i, (a, b) in enumerate(zip(observed, expected)):
                self.assertEqual(a, b, f'case {i}: {cases[i]}')
            self.assertGreater(negative_lengths, 100)
            self.assertGreater(saturated_sums, 100)
        finally:
            original.exe = before


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--table', type=Path)
    args, rest = parser.parse_known_args()
    RUNNER, UPSTREAM = args.runner.resolve(), args.upstream.resolve()
    TABLE = args.table
    unittest.main(argv=[__file__]+rest)
