"""A procedural or frozen control must never qualify full-Paul release cadence."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import saturn_workload as workload


class WorkloadTests(unittest.TestCase):
    def fixture(self):
        return dict(kind='original-kmd', shape=dict(workload.PAUL_SHAPE),
                    original_textures=True, archive_bytes=81885, archive_sha256='a' * 64,
                    input_sha256={'T3_LOCAL_KMD': 'b' * 64}, build_type='Release',
                    configuration={**dict.fromkeys(workload.REQUIRED_ON, 'ON'),
                                   **dict.fromkeys(workload.REQUIRED_OFF, 'OFF')})

    def test_release_and_both_guest_actors(self):
        good=self.fixture()
        state=dict(psx_owned_active=1, psx_combat_active=1, psx_round_flow_active=1,
                   psx_skin_preview_applied=[1, 1], psx_pose_status=[1, 1],
                   model_texture_count=724, model_archive_bytes=81885)
        workload.validate(good, 'two-paul-release', state)
        for key, value in [('psx_owned_active', 0), ('psx_skin_preview_applied', [1, 0]),
                           ('model_texture_count', 24), ('model_archive_bytes', 87962)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                workload.validate(good, 'two-paul-release', {**state, key: value})

    def test_procedural_frozen_instrumented_and_disabled_core_are_rejected(self):
        changes=[('T3_RENDER_BENCH_TICK', '120'), ('T3_PERF_MODE', '2'),
                 ('T3_ENABLE_PSX_OWNED_MATCH', 'OFF'), ('T3_VERIFY_PARALLEL_GEOMETRY', 'ON'),
                 ('T3_SIM_PROFILE', 'ON'), ('T3_FRAME_ON_SUBMIT', 'OFF')]
        for key, value in changes:
            bad=copy.deepcopy(self.fixture());bad['configuration'][key]=value
            with self.subTest(key=key), self.assertRaises(ValueError):
                workload.validate(bad, 'two-paul-release')
            workload.validate(bad, 'diagnostic')
        for key, value in [('kind', 'procedural'), ('original_textures', False),
                           ('shape', {**workload.PAUL_SHAPE, 'vertices': 143}),
                           ('input_sha256', {}), ('archive_sha256', None), ('build_type', 'Debug')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                workload.validate({**self.fixture(), key: value}, 'two-paul-release')


if __name__ == '__main__':
    unittest.main()
