"""The Saturn renderer must not draw a fighter the round flow has made inactive.

src/saturn/characters.c only builds for the SH-2, so this reads its source. It fails when a submission path
loses its gate or a new one appears, which then has to be gated and counted here. test_psx_match.c tests the rule
and scripts/saturn_combat_suite.py --with-ko tests the running game.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHARACTERS = (ROOT / 'src/saturn/characters.c').read_text()
MAIN = (ROOT / 'src/saturn/main.c').read_text()


def body(source, name):
    """The text of function `name`, from its definition to the closing brace in column 0."""
    start = re.search(rf'^[^\s#/*].*\b{name}\(.*\{{\s*$', source, re.M)
    if not start:
        raise AssertionError(f'{name} not found')
    return source[start.end():source.index('\n}\n', start.end())]


class Gate(unittest.TestCase):
    def test_every_submission_path_is_counted(self):
        self.assertEqual(len(re.findall(r'\bsat_scene_submit_instance\(', CHARACTERS)), 6,
                         'a fighter submission path changed: gate it on the fighter being shown, then update this count')

    def test_the_fused_path_reads_and_obeys_the_flag(self):
        fused = body(CHARACTERS, 'prepare_fused_actors')
        self.assertIn('t3_move_probe_fighter_shown(0)', fused)
        self.assertIn('t3_move_probe_fighter_shown(1)', fused)
        self.assertIn('g_fused_actors.shown[1]', fused)
        self.assertIn('g_fused_actors.shown[i]', fused, 'a hidden fighter must not take the original projection')
        ranged = body(CHARACTERS, 'prepare_actor_range')
        self.assertIn('context->shown[0]', ranged)
        self.assertIn('->shown[1]', ranged)

    def test_the_other_paths_obey_the_flag(self):
        render = body(CHARACTERS, 't3_characters_render')
        self.assertIn('t3_move_probe_fighter_shown(0) && t3_move_probe_fighter_shown(1)', render)
        self.assertIn('t3_move_probe_fighter_shown(i)', render)

    def test_a_hidden_fighter_casts_no_shadow(self):
        self.assertIn('t3_move_probe_fighter_shown(i)', body(MAIN, 'drop_shadows'))


if __name__ == '__main__':
    unittest.main()
