import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import saturn_regression


class RegressionDiscTests(unittest.TestCase):
    def test_combat_gate_forwards_selected_cue(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            args = SimpleNamespace(build=root / 'build', workload='paul-law-release',
                                   bios=root / 'bios', sh2_cache='on', disc=root / 'music.cue',
                                   title_start_field=400)
            with patch.object(saturn_regression, 'run_stage',
                              return_value={'exit_code': 0, 'detail': {}}) as run:
                saturn_regression.combat_stage(args, root)
            command = run.call_args.args[1]
            self.assertEqual(command[command.index('--disc') + 1], str(args.disc))
            self.assertEqual(command[command.index('--title-start-field') + 1], '400')
            self.assertIn('--with-ko', command)
            self.assertIn('--with-timeout', command)


if __name__ == '__main__':
    unittest.main()
