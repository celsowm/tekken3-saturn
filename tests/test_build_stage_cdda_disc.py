import sys
import tempfile
import unittest
import subprocess
import shutil
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_stage_cdda_disc import build


class MixedModeBuildTests(unittest.TestCase):
    def test_requires_cdda_configuration_and_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'CMakeCache.txt').write_text('T3_STAGE_CDDA:BOOL=OFF\n')
            with self.assertRaisesRegex(ValueError, 'T3_STAGE_CDDA=ON'):
                build(root, root / 'source.wav', root)
            (root / 'CMakeCache.txt').write_text('T3_STAGE_CDDA:BOOL=ON\n')
            (root / 'disc').mkdir()
            cue = root / 'disc/music.cue'
            cue.write_text('existing')
            with self.assertRaisesRegex(ValueError, 'exists'):
                build(root, root / 'source.wav', root)
            self.assertEqual(cue.read_text(), 'existing')

    @unittest.skipUnless(shutil.which('ffmpeg'), 'ffmpeg is required')
    def test_uses_project_builders_with_only_sdk_sector_encoder(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir, sdk = root / 'build', root / 'sdk'
            (build_dir / 'disc').mkdir(parents=True)
            (build_dir / 'CMakeCache.txt').write_text('T3_STAGE_CDDA:BOOL=ON\n')
            (build_dir / 'disc/tekken3_saturn.iso').write_bytes(bytes(2048))
            (sdk / 'tools').mkdir(parents=True)
            # Stand-in for the SDK's tracked sector encoder; the mixed-disc
            # builders must come from this project, not untracked SDK files.
            (sdk / 'tools/iso_to_raw.py').write_text(
                'ISO_SECTOR_BYTES=2048\nRAW_SECTOR_BYTES=2352\n'
                'def encode_mode1_sector(lba, data): return data + bytes(304)\n'
                'def validate_mode1_sector(lba, data): return data[:2048]\n')
            for name in ('build_cd_audio.py', 'convert_cd_audio.py'):
                (sdk / 'tools' / name).write_text('raise RuntimeError("wrong builder")\n')
            wav = root / 'source.wav'
            with wave.open(str(wav), 'wb') as audio:
                audio.setparams((2, 2, 44100, 588, 'NONE', 'not compressed'))
                audio.writeframes(bytes(2352))
            script = Path(__file__).resolve().parents[1] / 'scripts/build_stage_cdda_disc.py'
            subprocess.run([sys.executable, str(script), '--build', str(build_dir),
                            '--wav', str(wav), '--libsaturn', str(sdk)],
                           check=True, capture_output=True, text=True)
            self.assertEqual((build_dir / 'disc/music.bin').stat().st_size, 152 * 2352)
            self.assertIn('TRACK 02 AUDIO', (build_dir / 'disc/music.cue').read_text())


if __name__ == '__main__':
    unittest.main()
