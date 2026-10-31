import sys
import unittest
from unittest.mock import patch
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from check_cdda_capture import correlate, compare


class CddaCorrelationTests(unittest.TestCase):
    def test_finds_scaled_offset_in_mixed_signal(self):
        rng = np.random.default_rng(17)
        reference = rng.normal(size=4000)
        observed = reference[1350:2150] * 0.4 + rng.normal(scale=0.05, size=800)
        offset, score = correlate(reference, observed)
        self.assertEqual(offset, 1350)
        self.assertGreater(score, 0.98)

    def test_silence_does_not_produce_a_match(self):
        _, score = correlate(np.ones(100), np.zeros(20))
        self.assertEqual(score, 0)

    def test_timeline_constraint_avoids_an_unrelated_stronger_match(self):
        rng = np.random.default_rng(23)
        reference = rng.normal(size=3000)
        observed = reference[1200:1400] + rng.normal(scale=0.05, size=200)
        reference[200:400] = observed
        with patch('check_cdda_capture.decode', side_effect=[reference, observed]):
            result = compare(None, None, 22, 2, 30, 100, timeline_offset=10, tolerance=0.05)
        self.assertAlmostEqual(result['source_offset_seconds'], 12)
        self.assertGreater(result['correlation'], 0.99)


if __name__ == '__main__':
    unittest.main()
