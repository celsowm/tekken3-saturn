"""The Arcade music test must not reach the normal menu: every use of the engine in menu_main.c sits inside
T3_ARCADE_MUSIC_TEST, the engine is compiled only when T3_ARCADE_MUSIC_DIR is set, and the CD-DA calls of the
select screen are untouched."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def guarded_lines(text):
    """Line numbers (1-based) that are inside an #if defined(T3_ARCADE_MUSIC_TEST) ... #endif/#else block."""
    inside, depth, stack, out = False, 0, [], set()
    for number, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if s.startswith('#if'):
            stack.append(s.startswith('#if defined(T3_ARCADE_MUSIC_TEST)'))
        elif s.startswith('#else') and stack and stack[-1]:
            stack[-1] = False
        elif s.startswith('#endif') and stack:
            stack.pop()
        elif any(stack):
            out.add(number)
    return out


class Isolation(unittest.TestCase):
    def test_the_engine_is_used_only_under_its_guard(self):
        text = (ROOT / 'src/saturn/menu_main.c').read_text()
        guarded = guarded_lines(text)
        for number, line in enumerate(text.splitlines(), 1):
            if re.search(r't3_music_|g_t3_music|arcade_music\.h|g_arcade_music_on|music_test_flow', line) and not line.lstrip().startswith(('/*', '*', '//')):
                self.assertIn(number, guarded, f'menu_main.c:{number} uses the engine outside T3_ARCADE_MUSIC_TEST: {line.strip()}')

    def test_the_hook_is_empty_without_the_test(self):
        text = (ROOT / 'src/saturn/menu_main.c').read_text()
        self.assertRegex(text, r'#else\n#define MUSIC_TEST_HOOK\(p\)\n#endif')

    def test_the_engine_is_compiled_only_with_the_music_directory(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        block = re.search(r'if\(T3_ARCADE_MUSIC_DIR\)(.*?)\n        endif\(\)', cmake, re.S).group(1)
        self.assertIn('src/saturn/arcade_music.c', block)
        self.assertIn('T3_ARCADE_MUSIC_TEST=1', block)
        self.assertEqual(cmake.count('arcade_music.c'), 1)
        self.assertIn('set(T3_ARCADE_MUSIC_DIR ""', cmake)

    def test_the_select_screens_cdda_calls_are_still_there(self):
        text = (ROOT / 'src/saturn/menu_main.c').read_text()
        self.assertEqual(text.count('t3_select_cdda_start()'), 1)
        self.assertGreaterEqual(text.count('t3_stage_cdda_mix()'), 2)

    def test_the_engine_refuses_to_run_over_cdda(self):
        text = (ROOT / 'src/saturn/menu_main.c').read_text()
        self.assertRegex(text, r'cdda_playing\?SAT_ERR_BUSY:t3_music_start')


if __name__ == '__main__':
    unittest.main()
