import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import package_private_demo as package


class PrivateDemoPackageTests(unittest.TestCase):
    def test_rejects_mismatched_evidence_and_packages_matching_disc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build, output = root / 'build/candidate', root / 'build/demo.zip'
            (build / 'disc').mkdir(parents=True)
            (build / 'disc/music.cue').write_text('FILE "music.bin" BINARY\n  TRACK 01 MODE1/2352\n  TRACK 02 AUDIO\n')
            (build / 'disc/music.bin').write_bytes(b'disc')
            (build / 'tekken3_saturn.elf').write_bytes(b'elf')
            (root / 'third_party/Tekken3Remake').mkdir(parents=True)
            for name in ('LICENSE', 'NOTICE.md', 'third_party/Tekken3Remake/LICENSE.md'):
                (root / name).write_text('license')
            cue, disc, elf = (package.sha(build / path) for path in
                              ('disc/music.cue', 'disc/music.bin', 'tekken3_saturn.elf'))
            case = dict(elf_sha256=elf, provenance=dict(disc_sha256=cue,
                        disc_files_sha256={'music.bin': disc}))
            suite = dict(passed=True, native_round_replay_victory_validated=True,
                         original_vs_timeout_scoring_validated=True,
                         cases={str(index): case for index in range(26)})
            boot = {'returncode': 0, 'tekken3_saturn.elf_sha256': elf,
                    'cue_sha256': cue, 'cue_files': [dict(sha256=disc)]}
            suite_path, boot_path = root / 'suite.json', root / 'boot.json'
            suite_path.write_text(json.dumps(suite))
            boot_path.write_text(json.dumps({**boot, 'cue_sha256': 'wrong'}))
            with patch.object(package, 'ROOT', root):
                with self.assertRaisesRegex(ValueError, 'boot evidence'):
                    package.package(build, suite_path, boot_path, output)
                self.assertFalse(output.exists())
                boot_path.write_text(json.dumps(boot))
                result = package.package(build, suite_path, boot_path, output)
            self.assertEqual(result['combat_cases'], 26)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.read('music.bin'), b'disc')
                self.assertIn(b'private', archive.read('README.txt'))


if __name__ == '__main__':
    unittest.main()
