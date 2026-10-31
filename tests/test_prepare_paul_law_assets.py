import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import prepare_paul_law_assets as prepare
import build_paul_law_demo as demo
import check_demo_sdk


class DemoPreparationTests(unittest.TestCase):
    def test_private_output_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, 'private assets/imported'):
                prepare.prepare(Path(tmp) / 'disc.bin', Path(tmp) / 'output')
            with self.assertRaisesRegex(ValueError, 'private assets/imported'):
                demo.configure_command(Path(tmp) / 'assets', Path(tmp) / 'build',
                                       Path(tmp) / 'prefix', Path(tmp) / 'sdk', False)

    def test_missing_inputs_are_reported_before_configure(self):
        assets = demo.ROOT / 'assets/imported/unused-demo-preparation-test'
        with self.assertRaisesRegex(ValueError, 'missing build inputs'):
            demo.configure_command(assets, Path('/tmp/build'), Path('/tmp/prefix'), Path('/tmp/sdk'), False)

    def test_sdk_archive_requires_bitmap_api_and_enough_sound_slots(self):
        bitmap = '00000000 000001f0 T _sat_vdp2_rotation_bitmap_configure\n'
        sound = '00000000 {size:08x} B __ZN6saturn4core5audio8playback16g_sound_registryE\n'
        self.assertEqual(check_demo_sdk.inspect_symbols(bitmap + sound.format(size=64 * 28)), 64)
        with self.assertRaisesRegex(ValueError, '24 sound slots'):
            check_demo_sdk.inspect_symbols(bitmap + sound.format(size=24 * 28))
        with self.assertRaisesRegex(ValueError, 'rotation-bitmap'):
            check_demo_sdk.inspect_symbols(sound.format(size=64 * 28))
        with self.assertRaisesRegex(ValueError, 'registry layout'):
            check_demo_sdk.inspect_symbols(bitmap + sound.format(size=123))

    def test_build_command_forwards_explicit_boot_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets, prefix, sdk = root / 'assets/imported/demo', root / 'prefix', root / 'sdk'
            for name in list(demo.INPUTS.values()) + ['raw/036.bin', 'raw/056.bin', 'raw/072.bin',
                    'raw/080.bin', 'pose-tables/psx_sounds.bin', 'pose-tables/psx_effects.bin']:
                path = assets / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            for path in (prefix / 'share/libsaturn/toolchains/sh2eb-elf.cmake', sdk / 'include/saturn/core.h'):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            manifest = root / 'boot.json'
            manifest.touch()
            with patch.object(demo, 'ROOT', root), patch.object(demo, 'check_sdk'):
                command = demo.configure_command(assets, root / 'build', prefix, sdk, False, manifest)
                self.assertIn('-DT3_BOOT_PAYLOADS=' + str(manifest.resolve()), command)
                with self.assertRaisesRegex(ValueError, 'boot payload manifest'):
                    demo.configure_command(assets, root / 'build', prefix, sdk, False, root / 'missing')


if __name__ == '__main__':
    unittest.main()
