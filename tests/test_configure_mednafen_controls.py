import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import configure_mednafen_controls as controls


class MednafenControlsTests(unittest.TestCase):
    def test_backup_and_reconfigure_preserve_unrelated_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'mednafen.cfg'
            original = '; comment\n' + ''.join(
                f'ss.input.port1.gamepad.{key} old\n' for key in controls.KEYS)
            original += 'ss.input.port2 gamepad\nother.setting unchanged\n'
            path.write_text(original)
            backup = controls.configure(path)
            self.assertEqual(backup.read_text(), original)
            updated = path.read_text()
            self.assertIn('ss.input.port1.gamepad.x keyboard 0x0 4\n', updated)
            self.assertIn('ss.input.port1.gamepad.y keyboard 0x0 22\n', updated)
            self.assertIn('ss.input.port1.gamepad.z keyboard 0x0 7\n', updated)
            self.assertIn('ss.input.port1.gamepad.a keyboard 0x0 29\n', updated)
            self.assertIn('ss.input.port1.gamepad.b keyboard 0x0 27\n', updated)
            self.assertIn('ss.input.port1.gamepad.c keyboard 0x0 6\n', updated)
            self.assertIn('ss.input.port1.gamepad.ls keyboard 0x0 20\n', updated)
            self.assertIn('ss.input.port1.gamepad.rs keyboard 0x0 26\n', updated)
            self.assertIn('ss.input.port1.gamepad.up keyboard 0x0 82\n', updated)
            self.assertIn('ss.input.port1.gamepad.down keyboard 0x0 81\n', updated)
            self.assertIn('ss.input.port1.gamepad.left keyboard 0x0 80\n', updated)
            self.assertIn('ss.input.port1.gamepad.right keyboard 0x0 79\n', updated)
            self.assertIn('ss.input.port2 none\n', updated)
            self.assertIn('other.setting unchanged\n', updated)
            self.assertIsNone(controls.configure(path))

    def test_missing_setting_does_not_modify_config(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'mednafen.cfg'
            path.write_text('ss.input.port2 gamepad\n')
            with self.assertRaises(ValueError):
                controls.configure(path)
            self.assertEqual(path.read_text(), 'ss.input.port2 gamepad\n')
            self.assertEqual(list(Path(directory).iterdir()), [path])


if __name__ == '__main__':
    unittest.main()
