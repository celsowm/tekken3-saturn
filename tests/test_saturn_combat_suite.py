"""A torn debugger snapshot may retry; guest failures must remain failures."""
import argparse
from pathlib import Path
import sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import saturn_combat_suite as suite
import saturn_probe


class CoherentCaseTests(unittest.TestCase):
    def test_torn_snapshot_is_retained_and_actual_endpoint_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);inputs=root/'input.devices';inputs.write_text('0 1 buttons=X\n')
            args=argparse.Namespace(out=root,frames=155,device_script=inputs)
            calls=[]
            def run(a):
                calls.append(a.frames)
                (root/'diagnostics.bin').write_bytes(bytes([a.frames]))
                if a.frames==155:raise saturn_probe.IncompleteSnapshotError('torn')
                return dict(emulator_frames=a.frames)
            result=suite.coherent_case(args,run)
            self.assertEqual(calls,[155,156]);self.assertEqual(args.frames,155)
            self.assertEqual(result['emulator_frames'],156)
            self.assertEqual(result['publication_retry_fields'],1)
            self.assertEqual((root/'incomplete-155/diagnostics.bin').read_bytes(),bytes([155]))
            self.assertEqual((root/'incomplete-155/input.devices').read_text(),inputs.read_text())

    def test_guest_errors_never_retry(self):
        args=argparse.Namespace(frames=155)
        calls=[]
        def run(a):
            calls.append(a.frames);raise ValueError('native guest panic')
        with self.assertRaisesRegex(ValueError,'native guest panic'):
            suite.coherent_case(args,run)
        self.assertEqual(calls,[155])

    def test_repeated_torn_snapshots_fail_after_three_attempts(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);inputs=root/'input.devices';inputs.write_text('')
            args=argparse.Namespace(out=root,frames=155,device_script=inputs)
            calls=[]
            def run(a):
                calls.append(a.frames);raise saturn_probe.IncompleteSnapshotError('torn')
            with self.assertRaises(saturn_probe.IncompleteSnapshotError):suite.coherent_case(args,run)
            self.assertEqual(calls,[155,156,157])


if __name__=='__main__':unittest.main()
