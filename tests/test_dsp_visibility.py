"""Check the experimental SCU DSP face predicate against the world-space oracle."""

import os
import random
import re
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("LIBSATURN_SOURCE_DIR", ROOT / ".external/libsaturn"))
if not (SDK / "tools/scu_dsp.py").exists():
    SDK = ROOT.parent / "lib-saturn"
sys.path.insert(0, str(SDK / "tools"))
import scu_dsp  # noqa: E402


def cross(u, v):
    return (u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0])


def oracle(vertices, face, eye):
    a, b, c, d = (vertices[i] for i in face)
    sub = lambda x, y: tuple(x[k] - y[k] for k in range(3))
    normal = cross(sub(d, a), sub(b, a))
    if not any(normal):
        normal = cross(sub(d, a), sub(c, a))
    if not any(normal):
        normal = cross(sub(c, a), sub(b, a))
    # The source coordinates are world=6*q; C++ integer division truncates to 0.
    centre = tuple(int(6 * sum(v[k] for v in (a, b, c, d)) / 4)
                   for k in range(3))
    return sum(normal[k] * (eye[k] - centre[k]) for k in range(3)) > 0


class DspVisibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/saturn/dsp_visibility.dsp").read_text()
        cls.words, _ = scu_dsp.assemble(source, 256)

    def run_tile(self, vertices, faces, eye):
        self.assertLessEqual(len(vertices), 48)
        self.assertLessEqual(len(faces), 16)
        self.assertTrue(1 <= len(faces))
        self.assertTrue(all(max(v[k] for v in vertices) - min(v[k] for v in vertices)
                            <= 32767 for k in range(3)))
        self.assertTrue(all(abs(e) < (1 << 19) for e in eye))
        sim = scu_dsp.DspSim()
        sim.load(self.words)
        for i, v in enumerate(vertices):
            for k in range(3):
                sim.data[k][i] = v[k] & 0xffffffff
        for i, face in enumerate(faces):
            sim.data[3][i] = sum(idx << (24 - 8 * k)
                                 for k, idx in enumerate(face))
        for k in range(3):
            sim.data[2][48 + k] = eye[k] & 0xffffffff
        sim.data[2][61:63] = [1 << 25, 0xfc000000]
        sim.data[0][51:58] = [0, len(faces), 0, 1, 32, 0, 0]
        originals = [bank[:len(vertices)] for bank in sim.data[:3]]
        sim.start()
        sim.run(100000)
        self.assertEqual([bank[:len(vertices)] for bank in sim.data[:3]], originals)
        visible, fallback = sim.data[0][53], sim.data[0][57]
        self.assertEqual((visible | fallback) >> len(faces), 0)
        self.assertEqual(visible & fallback, 0)
        for i, face in enumerate(faces):
            a, b, _, d = (vertices[j] for j in face)
            n = cross(tuple(d[k] - a[k] for k in range(3)),
                      tuple(b[k] - a[k] for k in range(3)))
            admitted = any(n) and all(-(1 << 25) <= x < (1 << 25)
                                      for x in n)
            self.assertEqual(bool(fallback & (1 << i)), not admitted,
                             (i, face, n))
            if admitted:
                self.assertEqual(bool(visible & (1 << i)),
                                 oracle(vertices, face, eye),
                                 (i, face, n, eye))
        return visible, fallback

    def test_program_image_matches_assembly_and_fits_pram(self):
        self.assertEqual(len(self.words), 197)
        header = (ROOT / "src/saturn/dsp_visibility_words.h").read_text()
        hex_words = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{8})u", header)]
        self.assertEqual(hex_words, self.words)

    def test_full_tile_shared_vertices_and_near_edge_on_faces(self):
        rng = random.Random(0x51A7)
        for _ in range(120):
            origin = [rng.randrange(-12000, 12001) for _ in range(3)]
            vertices = [tuple(origin[k] + rng.randrange(-800, 801)
                              for k in range(3)) for _ in range(48)]
            faces = [tuple(rng.randrange(48) for _ in range(4))
                     for _ in range(16)]
            eye = tuple(rng.randrange(-200000, 200001) for _ in range(3))
            self.run_tile(vertices, faces, eye)

    def test_collapsed_corners_and_normal_range_fallback(self):
        vertices = [(0, 0, 0), (5, 0, 0), (5, 0, 5), (0, 0, 5),
                    (32000, 0, 0), (0, 32000, 0)]
        faces = [(0, 0, 2, 3), (0, 1, 2, 0), (0, 1, 2, 3),
                 (0, 4, 3, 5)]
        _, fallback = self.run_tile(vertices, faces, (0, 600, 0))
        self.assertEqual(fallback, 0b1011)


if __name__ == "__main__":
    unittest.main()
