"""Synthetic USA PS1-RAM decoder regressions; no copyrighted disc data."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/psx_ram_oracle.py"
SPEC = importlib.util.spec_from_file_location("psx_ram_oracle", SCRIPT)
oracle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(oracle)


def put(ram, guest_address, fmt, value):
    struct.pack_into("<" + fmt, ram, guest_address - 0x80000000, value)


def ram_image(frame=1, state=8):
    ram = bytearray(oracle.RAM_BYTES)
    put(ram, oracle.GAME_STATE, "I", state)
    put(ram, oracle.GLOBAL_FRAME, "I", frame)
    for i in range(2):
        base = oracle.FIGHTER_BASE + i * oracle.FIGHTER_STRIDE
        put(ram, base + 0x1A0, "h", 3 + i)
        put(ram, base + 0x58, "h", 7 + i)
        put(ram, base + 0x3F4, "i", (500 - 10 * i) * 65536)
        put(ram, base + 0x3F8, "i", 1000 * 65536)
        put(ram, base + 0x60, "I", 0x842)
        put(ram, base + 0x5C, "h", 12)
        put(ram, base + 0x76, "h", 3)
        put(ram, base + 0x11A, "h", -1)
        put(ram, base + 0xCE, "B", i)
        put(ram, base + 0xCF, "B", i)
        put(ram, base + 0xD0, "B", i)
        put(ram, base + 0xD3, "B", i)
        put(ram, base + 0xDB, "B", i)
        put(ram, base, "i", -(500 + i))
        put(ram, base + 4, "i", 12345)
        put(ram, base + 8, "i", 987)
        put(ram, base + 0x0E, "H", 0x8000)
    return ram


class PS1RamOracleTests(unittest.TestCase):
    def test_addresses_match_pinned_usa_release_mapping(self):
        self.assertEqual(oracle.FIGHTER_BASE, 0x800A9228)
        self.assertEqual(oracle.FIGHTER_STRIDE, 0x188C)
        self.assertEqual(oracle.GLOBAL_FRAME, 0x800AFA4C)
        self.assertEqual(oracle.GAME_STATE, 0x800AE204)
        lua = (Path(__file__).resolve().parents[1] /
               "scripts/pcsx_redux_capture_usa.lua").read_text()
        for needle in ["0x800A9228", "0x800AFA4C", "0x800AE204",
                       "0x188C", "0x1A0", "0x3F4", "0x3F8"]:
            self.assertIn(needle, lua)

    def test_extraction_of_both_fighters_and_raw_signed_fields(self):
        guest_frame, rows = oracle.decode_snapshot(ram_image(99), 4)
        self.assertEqual(guest_frame, 99)
        self.assertEqual([(r["tick"], r["fighter"], r["move_slot"])
                          for r in rows], [(4, 0, 3), (4, 1, 4)])
        self.assertEqual(rows[1]["pose_frame"], 8)
        self.assertEqual(rows[1]["health_q16"], 490 * 65536)
        self.assertEqual(rows[0]["psx"]["recover_frames"], -1)
        self.assertEqual(rows[0]["psx"]["root_x"], -500)
        self.assertEqual(rows[0]["psx"]["facing"], 0x8000)
        self.assertNotIn("phase", rows[0])
        self.assertNotIn("combo_hits_received", rows[0])
        self.assertNotIn("frames_left", rows[0])
        self.assertEqual(rows[0]["ko"], 0)

    def test_reject_wrong_build_and_corrupt_samples(self):
        with self.assertRaisesRegex(oracle.InvalidSnapshot, "not FightMain"):
            oracle.decode_snapshot(ram_image(state=7), 0)
        with self.assertRaisesRegex(oracle.InvalidSnapshot, "exactly"):
            oracle.decode_snapshot(bytes(1000), 0)
        ram = ram_image()
        put(ram, oracle.FIGHTER_BASE + 0x3F4, "i", -2)
        with self.assertRaisesRegex(oracle.InvalidSnapshot, "health"):
            oracle.decode_snapshot(ram, 0)
        ram = ram_image()
        put(ram, oracle.FIGHTER_BASE + 0x1A0, "h", -1)
        with self.assertRaisesRegex(oracle.InvalidSnapshot, "move/pose"):
            oracle.decode_snapshot(ram, 0)

    def test_sequential_snapshots_and_atomic_rejection(self):
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d)
            one, two = directory / "0000.bin", directory / "0001.bin"
            output = directory / "original.psx-combat.jsonl"
            one.write_bytes(ram_image(77))
            two.write_bytes(ram_image(78))
            self.assertEqual(oracle.write_trace([one, two], output), 2)
            data = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(len(data), 4)
            self.assertEqual([r["tick"] for r in data], [0, 0, 1, 1])
            self.assertEqual([r["source_frame"] for r in data],
                             [77, 77, 78, 78])
            snapshot = output.read_bytes()
            two.write_bytes(ram_image(80))
            with self.assertRaisesRegex(oracle.InvalidSnapshot, "non-consecutive"):
                oracle.write_trace([one, two], output)
            self.assertEqual(output.read_bytes(), snapshot)
            two.write_bytes(ram_image(77))
            with self.assertRaises(oracle.InvalidSnapshot):
                oracle.write_trace([one, two], output)
            self.assertEqual(output.read_bytes(), snapshot)

    def test_guest_counter_wrap_and_cli_validation(self):
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d)
            one, two = directory / "a.bin", directory / "b.bin"
            output = directory / "original.psx-combat.jsonl"
            one.write_bytes(ram_image(0xFFFFFFFF))
            two.write_bytes(ram_image(0))
            self.assertEqual(oracle.main(["--output", str(output),
                                          str(two), str(one)]), 0)
            self.assertEqual(len(output.read_text().splitlines()), 4)
            self.assertEqual(oracle.main(["--output", str(output),
                                          str(one), str(one)]), 2)


if __name__ == "__main__":
    unittest.main()
