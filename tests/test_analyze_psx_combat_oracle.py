"""No-retail-data test harness for raw PS1 combat trace event reporting."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/analyze_psx_combat_oracle.py"
SPEC = importlib.util.spec_from_file_location("analyze_psx_combat_oracle", SCRIPT)
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)


def row(tick, fighter, **changes):
    raw = dict(state=0x842, move_frame=tick, hit_freeze=0,
               recover_frames=0, got_hit=0, guarded=0,
               hit_clean=0, counter_hit=0, in_air=0,
               root_x=0, root_y=0, root_z=0, facing=32768)
    r = dict(tick=tick, source_frame=100+tick, fighter=fighter,
             move_slot=3, pose_frame=tick, health_q16=500*65536,
             ko=0, psx=raw)
    for key, value in changes.items():
        if key.startswith("raw_"):
            raw[key[4:]] = value
        else:
            r[key] = value
    return r


def example():
    data = []
    for tick in range(4):
        p0 = row(tick, 0)
        p1 = row(tick, 1)
        if tick >= 1:
            p0["move_slot"] = 42
            p1["health_q16"] = 450 * 65536
            p1["psx"]["got_hit"] = 1
        if tick == 2:
            p0["pose_frame"] = 1
        if tick >= 2:
            p1["ko"] = 1
        data.extend((p0, p1))
    return data


def write(path, records):
    path.write_text("".join(json.dumps(r) + "\n" for r in records))


class OracleEventsTests(unittest.TestCase):
    def test_observed_changes_without_stun_fabrication(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "ps1.psx-combat.jsonl"
            write(path, example())
            ticks = analyzer.read_capture(path)
            report = analyzer.analyze(ticks)
            self.assertEqual(report["summary"]["frames"], 4)
            self.assertEqual(report["summary"]["hp_changed"], [0, 1])
            self.assertEqual(report["summary"]["moves_changed"], [1, 0])
            self.assertEqual(report["summary"]["ko_edges"], [0, 1])
            self.assertEqual(report["summary"]["pose_static_runs"], [1, 0])
            self.assertTrue(any(x["kind"]=="pose_static_interval" and
                                x["fighter"]==0 and x["first_tick"]==1 and
                                x["last_tick"]==2 and x["observations"]==2
                                for x in report["events"]))
            self.assertTrue(any(x["kind"]=="health_changed" and
                                x["delta_q16"]==-50*65536
                                for x in report["events"]))
            self.assertNotIn("hitstun_frames", report)
            self.assertEqual(report["schema"], "t3-ps1-observed-combat-v1")

    def test_rejects_incomplete_pair_missing_raw_or_bad_guest_frame(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.psx-combat.jsonl"
            for scenario in ("missing", "wrong_counter", "missing_psx",
                             "bool_flag", "gap"):
                records = example()
                if scenario=="missing":
                    records.pop()
                elif scenario=="wrong_counter":
                    records[3]["source_frame"] = 1200
                elif scenario=="missing_psx":
                    records[0]["psx"].pop("recover_frames")
                elif scenario=="bool_flag":
                    records[0]["psx"]["guarded"] = True
                elif scenario=="gap":
                    for r in records:
                        if r["tick"]>=2:
                            r["source_frame"] += 1
                write(path, records)
                with self.subTest(scenario=scenario):
                    with self.assertRaises(analyzer.InvalidOracle):
                        analyzer.read_capture(path)

    def test_cli_outputs_json_and_rejects_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            ps1 = directory / "original.psx-combat.jsonl"
            output = directory / "events.json"
            write(ps1, example())
            self.assertEqual(analyzer.main(["--ps1", str(ps1),
                                            "--output", str(output)]), 0)
            report = json.loads(output.read_text())
            self.assertEqual(report["summary"]["frames"], 4)
            before = output.read_bytes()
            ps1.write_text("{ malformed\n")
            self.assertEqual(analyzer.main(["--ps1", str(ps1),
                                            "--output", str(output)]), 2)
            self.assertEqual(output.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
