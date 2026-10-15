"""Synthetic frame-by-frame combat oracle comparator regressions."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/compare_psx_combat_traces.py'
spec = importlib.util.spec_from_file_location('compare_psx_combat_traces', SCRIPT)
comparator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparator)


def rows():
    return [dict(tick=tick, fighter=fighter, move_slot=42 if tick else 3,
                 pose_frame=tick, phase=1 if tick else 0, phase_frame=tick,
                 frames_left=3-tick if tick else 0,
                 combo_hits_received=1 if tick else 0,
                 health_q16=400*65536, ko=0)
            for tick in range(3) for fighter in range(2)]


def by_key(records):
    return {(r['tick'], r['fighter']): r for r in records}


class CombatTraceTests(unittest.TestCase):
    def test_identical_and_frame_difference(self):
        original = rows()
        clone = rows()
        self.assertEqual(comparator.compare(by_key(original), by_key(clone)), [])
        clone[3]['move_slot'] = 47
        differences = comparator.compare(by_key(original), by_key(clone))
        self.assertEqual(len(differences), 1)
        self.assertIn('tick 1 P2 move_slot: PS1=42 Saturn=47', differences[0])

    def test_parser_rejects_bad_frames_and_dupes(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'trace.jsonl'
            examples = [rows(), rows()[:-1], rows() + [rows()[0]],
                        [r for r in rows() if r['tick'] != 1],
                        [dict(r, pose_frame=True) if i == 0 else r
                         for i, r in enumerate(rows())]]
            for index, sample in enumerate(examples):
                source.write_text(''.join(json.dumps(row)+'\n' for row in sample))
                if index == 0:
                    self.assertEqual(len(comparator.load(source)), 6)
                else:
                    with self.assertRaises(comparator.InvalidTrace):
                        comparator.load(source)

    def test_optional_missing_is_a_mismatch(self):
        a = rows()
        b = rows()
        del b[1]['combo_hits_received']
        differences = comparator.compare(by_key(a), by_key(b))
        self.assertEqual(len(differences), 1)
        self.assertIn('missing from Saturn', differences[0])

    def test_cli_exit_codes(self):
        with tempfile.TemporaryDirectory() as temporary:
            reference = Path(temporary)/'ps1.jsonl'
            saturn = Path(temporary)/'saturn.jsonl'
            reference.write_text(''.join(json.dumps(r)+'\n' for r in rows()))
            saturn.write_text(reference.read_text())
            self.assertEqual(comparator.main(['--ps1', str(reference),
                                              '--saturn', str(saturn)]), 0)
            saturn.write_text('oops\n')
            self.assertEqual(comparator.main(['--ps1', str(reference),
                                              '--saturn', str(saturn)]), 2)


if __name__ == '__main__':
    unittest.main()
