"""Synthetic VS screen packer tests; no retail fixtures."""
import struct
import sys
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import pack_vs_art as vs


def face(colour=(40, 80, 120)):
    picture = np.zeros((256, vs.PORTRAIT_W, 4), np.uint8)
    picture[:, :, :3] = colour
    picture[:, :, 3] = 255
    return picture


class Positions(unittest.TestCase):
    def test_finds_the_table_anywhere_in_the_overlay(self):
        values = (12, 100, -160, 0, 226, 210, 160, 0, 149, 106, 0, 0, -219, 390, 0, 0)
        table = b'junk' + struct.pack('<16h', *values) + b'tail'
        positions = vs.positions(table)
        self.assertEqual(positions['portraits'], ((12, 100), (226, 210)))
        self.assertEqual(positions['names'], ((149, 106), (-219, 390)))

    def test_refuses_another_overlay(self):
        with self.assertRaises(ValueError):
            vs.positions(b'\0' * 64)


class Names(unittest.TestCase):
    def test_positive_x_is_the_left_edge(self):
        self.assertEqual(vs.name_position((149, 106), 84), 149)

    def test_negative_x_is_the_right_edge(self):
        self.assertEqual(vs.name_position((-219, 390), 84), 135)

    def test_kept_inside_the_screen(self):
        self.assertEqual(vs.name_position((-20, 390), 84), 12)
        self.assertEqual(vs.name_position((300, 106), 84), 272)


class Panel(unittest.TestCase):
    record = dict(tint=(100, 0, 0))

    def canvas(self):
        return np.full((vs.SOURCE_H, vs.SOURCE_W, 3), 10, np.uint8)

    def test_frame_surrounds_the_portrait(self):
        out = vs.panel(self.canvas(), face(), self.record, (12, 100), False)
        self.assertEqual(out.shape, (vs.FRAME_H, vs.FRAME_W, 3))
        for texel in (out[0, 0], out[223, 129], out[100, 0], out[100, 129]):
            self.assertEqual(tuple(texel), vs.FRAME_GREY)
        self.assertEqual(tuple(out[100, 60]), (40, 80, 120))

    def test_mirroring_flips_the_portrait(self):
        picture = face()
        picture[:, 0, :3] = (200, 0, 0)
        plain = vs.panel(self.canvas(), picture, self.record, (12, 100), False)
        mirrored = vs.panel(self.canvas(), picture, self.record, (12, 100), True)
        self.assertEqual(tuple(plain[100, 2]), (200, 0, 0))
        self.assertEqual(tuple(mirrored[100, 127]), (200, 0, 0))

    def test_tint_lights_what_shows_through_only(self):
        picture = face()
        picture[:, :, 3] = 0
        out = vs.panel(self.canvas(), picture, self.record, (12, 100), False)
        # Black to the colour over the upper half, the colour to grey over the lower.
        self.assertEqual(tuple(out[6, 60]), (10, 10, 10))
        self.assertEqual(tuple(out[111, 60]), (110, 10, 10))
        self.assertEqual(tuple(out[217, 60]), (202, 202, 202))


class Bar(unittest.TestCase):
    def test_opaque_then_fading(self):
        canvas = np.full((vs.SOURCE_H, vs.SOURCE_W, 3), 50, np.uint8)
        vs.caption_bar(canvas, 4)
        colour, width = vs.BARS[4]
        y = vs.BAR_AT[1]
        self.assertEqual(tuple(canvas[y, 0]), colour)
        self.assertEqual(tuple(canvas[y, width - vs.BAR_TAIL - 1]), colour)
        self.assertEqual(tuple(canvas[y, width]), (50, 50, 50))
        self.assertEqual(tuple(canvas[y - 1, 0]), (50, 50, 50))


class Indexed(unittest.TestCase):
    def test_palette_is_exactly_the_size_asked(self):
        for count in (vs.BACKGROUND_COLOURS, vs.PANEL_COLOURS):
            image = Image.fromarray(np.random.default_rng(1).integers(0, 255, (32, 32, 3), np.uint8))
            codes, words = vs.indexed(image, count, 0, True)
            self.assertEqual(len(words), count)
            self.assertLess(int(codes.max()), count)
            self.assertGreaterEqual(int(codes.min()), 0)

    def test_codes_start_where_asked(self):
        image = Image.new('RGB', (4, 4), (255, 0, 0))
        codes, _ = vs.indexed(image, 8, 96, False)
        self.assertTrue((codes >= 96).all())


class Layout(unittest.TestCase):
    def test_code_ranges_do_not_overlap(self):
        self.assertEqual(vs.BACKGROUND_COLOURS + 3 * vs.GLYPH_COLOURS, vs.CREDIT_CODE)
        self.assertEqual(vs.CREDIT_CODE + 1, vs.SIDE_CODES[0])
        self.assertEqual(vs.SIDE_CODES[0] + vs.PANEL_COLOURS, vs.SIDE_CODES[1])
        self.assertEqual(vs.SIDE_CODES[1] + vs.PANEL_COLOURS, 256)

    def test_panels_are_even_for_the_bitmap(self):
        self.assertEqual(vs.PANEL_W % 2, 0)


if __name__ == '__main__':
    unittest.main()
