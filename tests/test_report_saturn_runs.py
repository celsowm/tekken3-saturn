import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from report_saturn_runs import cadence, summarize


class RunReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def probe(self, name, field, frames, ticks, elf='same'):
        path = self.root / name
        path.mkdir()
        data = dict(elf_sha256=elf, cd_boot_validated=False, state=dict(
            frame_serial=frames + 1, submitted_frames=frames, vblanks=field,
            sim_ticks=ticks, first_error=0, draw_timeouts=0,
            commands_used=100, commands_capacity=2048))
        (path / 'observation.json').write_text(json.dumps(data))
        return path

    def test_cadence_and_provenance(self):
        a = self.probe('start', 581, 78, 316)
        b = self.probe('end', 1183, 160, 644)
        result = cadence(a, b, 60)
        self.assertAlmostEqual(result['submissions_per_second'], 8.172757475)
        self.assertAlmostEqual(result['logic_ticks_per_second'], 32.6910299)
        c = self.probe('other-build', 1183, 160, 644, elf='different')
        with self.assertRaisesRegex(ValueError, 'same ELF'):
            cadence(a, c, 60)
        with self.assertRaisesRegex(ValueError, 'advance'):
            cadence(b, a, 60)

    def test_discarded_time_is_reported_separately_from_committed_ticks(self):
        paths=[self.probe('start',600,100,400),self.probe('end',1800,300,1200)]
        for path,observed,discarded,scheduled in zip(paths,(598,1798),(198,598),(400,1200)):
            file=path/'observation.json';data=json.loads(file.read_text())
            data['state'].update(observed_fields=observed,discarded_fields=discarded,
                                 scheduled_ticks=scheduled)
            file.write_text(json.dumps(data))
        result=cadence(*paths,60)
        self.assertEqual(result['logic_ticks_per_second'],40)
        self.assertAlmostEqual(result['scheduling']['discarded_percent'],100/3)
        self.assertAlmostEqual(result['scheduling']['discarded_seconds'],400/60)

    def test_pose_reuse_interval_and_fight_reset(self):
        a=self.probe('a',600,100,400);b=self.probe('b',1800,300,1200)
        def update(path,values):
            f=path/'observation.json';data=json.loads(f.read_text())
            data['state']['local_pose_cache']=values;f.write_text(json.dumps(data))
        update(a,[128,500,100,0]);update(b,[128,2100,100,0])
        result=cadence(a,b,60)
        self.assertEqual(result['local_pose_cache']['hit_percent'],100)
        self.assertEqual(result['local_pose_cache']['hits'],1600)
        update(b,[128,20,10,0])
        result=cadence(a,b,60)
        self.assertEqual(result['logic_ticks_per_second'],40)
        self.assertFalse(result['local_pose_cache']['interval_available'])

    def test_native_panic_overrides_stale_success(self):
        path = self.probe('panic', 100, 12, 48)
        (path / 'native-panic.bin').write_bytes(struct.pack('>Iii32s', 0x50414E43, -7, 193, b'main.c'))
        report = summarize(path)
        self.assertFalse(report['passed'])
        self.assertEqual(report['native_panic']['code'], -7)

    def test_suite_rechecks_case_health(self):
        path = self.probe('bad', 100, 12, 48)
        data = json.loads((path / 'observation.json').read_text())
        data['state']['draw_timeouts'] = 1
        suite = self.root / 'suite.json'
        suite.write_text(json.dumps(dict(passed=True, cases={'bad': data})))
        self.assertFalse(summarize(suite)['passed'])
        self.assertEqual(summarize(suite)['failed_cases'], ['bad'])

    def test_mednafen_exit_failure(self):
        path = self.root / 'run.json'
        path.write_text(json.dumps(dict(returncode=1, seconds_requested=35, scope='CD boot')))
        self.assertFalse(summarize(path)['passed'])


if __name__ == '__main__':
    unittest.main()
