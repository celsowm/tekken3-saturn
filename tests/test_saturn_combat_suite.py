"""A torn debugger snapshot may retry; guest failures must remain failures."""
import argparse
from pathlib import Path
import sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import saturn_combat_suite as suite
import saturn_probe


class CoherentCaseTests(unittest.TestCase):
    def test_loading_offset_preserves_input_intervals(self):
        self.assertEqual(suite.delayed_script('20 1 buttons=RIGHT\n100 1 buttons=NONE\n',160),
                         '180 1 buttons=RIGHT\n260 1 buttons=NONE\n')
        with self.assertRaises(ValueError):suite.delayed_script('',-1)

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


class GameplayStartTests(unittest.TestCase):
    def test_start_is_found_within_resolution_for_any_boot_length(self):
        for boot in (0,7,91,276,640):
            with tempfile.TemporaryDirectory() as directory:
                args=argparse.Namespace(out=Path(directory),frames=0,device_script=None)
                def run(a):
                    if a.frames<boot//4:raise ValueError('unsupported diagnostics magic/version')
                    if a.frames<boot//2:raise ValueError('guest has not published a gameplay frame')
                    return dict(state=dict(sim_ticks=max(0,a.frames-boot)))
                found=suite.first_gameplay_field(args,run)
                self.assertTrue(boot<=found<=boot+10,(boot,found))

    def test_never_starting_simulation_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            args=argparse.Namespace(out=Path(directory),frames=0,device_script=None)
            with self.assertRaises(AssertionError):
                suite.first_gameplay_field(args,lambda a:dict(state=dict(sim_ticks=0)),limit=100)
