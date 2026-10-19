import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from compare_saturn_presentation import BOOT_FIELDS, compare


class PresentationComparison(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.paths = [Path(self.temp.name)/name for name in ('before', 'after')]
        for p in self.paths:
            p.mkdir()
            (p/'observation.json').write_text(json.dumps(dict(
                elf_sha256=p.name, provenance=dict(bios_sha256='bios', probe_sha256='probe'),
                state=dict(first_error=0, draw_timeouts=0, sim_ticks=120,
                           frame_serial=3, psx_pose_status=[1, 1]))))
            (p/'ymir.json').write_text(json.dumps(dict(
                boot={k: 1 for k in BOOT_FIELDS}, division_timing_model='test')))
            Image.new('RGB', (320, 224)).save(p/'screen.png')

    def edit(self, filename, mutate):
        p = self.paths[1]/filename
        data = json.loads(p.read_text())
        mutate(data)
        p.write_text(json.dumps(data))

    def test_serial_and_hud_pixels_can_change(self):
        self.edit('observation.json', lambda d: d['state'].update(frame_serial=500))
        im = Image.open(self.paths[1]/'screen.png')
        im.putpixel((1, 1), (255, 0, 0))
        im.save(self.paths[1]/'screen.png')
        self.assertTrue(compare(*self.paths)['passed'])

    def test_mismatched_tick_and_pose_are_rejected(self):
        for field, value in [('sim_ticks', 121), ('psx_pose_status', [0, 1])]:
            with self.subTest(field=field):
                self.edit('observation.json', lambda d: d['state'].update({field: value}))
                with self.assertRaises(ValueError):
                    compare(*self.paths)
                self.edit('observation.json', lambda d: d['state'].update(sim_ticks=120, psx_pose_status=[1, 1]))

    def test_cache_policy_is_required_to_match(self):
        self.edit('ymir.json', lambda d: d['boot'].update(sh2_cache_emulation=0))
        with self.assertRaisesRegex(ValueError, 'cache'):
            compare(*self.paths)

    def test_model_pixels_are_required_to_match(self):
        im = Image.open(self.paths[1]/'screen.png')
        im.putpixel((100, 100), (255, 0, 0))
        im.save(self.paths[1]/'screen.png')
        with self.assertRaisesRegex(ValueError, 'pixels'):
            compare(*self.paths)


if __name__ == '__main__':
    unittest.main()
