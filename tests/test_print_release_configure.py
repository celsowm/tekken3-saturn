import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class ReleaseConfigureTests(unittest.TestCase):
    def test_retains_false_empty_and_zero_options(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / 'baseline'
            build.mkdir()
            (build / 'CMakeCache.txt').write_text(
                'T3_PARTICLES:BOOL=OFF\nT3_LOCAL_ACTOR1_KMD:FILEPATH=\n'
                'T3_RENDER_BENCH_TICK:STRING=0\nT3_EXAMPLE:UNINITIALIZED=hello world\n'
                'CMAKE_BUILD_TYPE:STRING=Release\nIGNORED:STRING=value\n')
            output = subprocess.check_output(
                [sys.executable, str(root / 'scripts/print_release_configure.py'), str(build), 'new build'],
                text=True)
            args = shlex.split(output.replace('\\\n', ''))
            self.assertIn('new build', args)
            self.assertIn('-DT3_PARTICLES:BOOL=OFF', args)
            self.assertIn('-DT3_LOCAL_ACTOR1_KMD:FILEPATH=', args)
            self.assertIn('-DT3_RENDER_BENCH_TICK:STRING=0', args)
            self.assertIn('-DT3_EXAMPLE:STRING=hello world', args)
            self.assertNotIn('-DIGNORED:STRING=value', args)


if __name__ == '__main__':
    unittest.main()
