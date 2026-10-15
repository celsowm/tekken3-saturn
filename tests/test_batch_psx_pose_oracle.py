"""Batch private DIVMOT move coverage, entirely synthetic in CI."""
import argparse
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/"scripts"),str(ROOT/"tests")]
import batch_psx_pose_oracle as batcher
from test_divmot_to_logic import fake_bank
from test_psx_motion_export import animated_bank
from test_diff_psx_solver import synthetic_tables

EXPORTER=None
SOLVER=None
UPSTREAM=None

def fixture():
    char=bytearray(animated_bank())
    # local move on slots 0 and 2; common move in slot 1.
    struct.pack_into("<I",char,132+4,0x40000000)
    struct.pack_into("<I",char,132+8,0)
    return bytes(char),animated_bank(99)

class Inspect(unittest.TestCase):
    def test_enumeration_deduplicates_and_skips_sparse_slots(self):
        char,common=fixture()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/"char.bin").write_bytes(char)
            (root/"common.bin").write_bytes(common)
            c=batcher.read_bank(root/"char.bin")
            g=batcher.read_bank(root/"common.bin",common=True)
        items,reasons=batcher.discover(c,g,[0,1,2,3])
        self.assertEqual(len(items),2)
        self.assertEqual([i["slot"] for i in items],[0,1])
        self.assertEqual(items[0]["duplicate_slots"],1)
        self.assertEqual(items[1]["owner"],99)
        self.assertEqual(items[1]["animation_bank"],99)
        self.assertEqual(items[0]["frames"],4)
        self.assertEqual(reasons["empty"],1)
        items,reasons=batcher.discover(c,None,[1,3])
        self.assertEqual(items,[])
        self.assertEqual(reasons["missing_common_bank"],1)
        self.assertEqual(reasons["empty"],1)
    def test_ranges_and_spread_are_deterministic(self):
        self.assertEqual(batcher.slot_indices("3-5,1,3"),[1,3,4,5])
        self.assertEqual(batcher.spread(list(range(11)),3),[0,5,10])
        self.assertEqual(batcher.spread(list(range(11)),1),[0])
        for spec in ("-1","1-0","4023","abc","1,","1--2"):
            with self.subTest(spec=spec),self.assertRaises(batcher.BatchError):
                batcher.slot_indices(spec)
        self.assertEqual(len(batcher.slot_indices(None)),4023)
    def test_malformed_bank_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"broken.bin"
            path.write_bytes(b"invalid")
            with self.assertRaises(batcher.BatchError):
                batcher.read_bank(path)
            path.write_bytes(fake_bank(99))
            with self.assertRaises(batcher.BatchError):
                batcher.read_bank(path)
            path.write_bytes(fake_bank())
            with self.assertRaises(batcher.BatchError):
                batcher.read_bank(path,common=True)

class Integration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for label,path in (("exporter",EXPORTER),("solver",SOLVER),("upstream",UPSTREAM)):
            if not path or not path.is_file():
                raise AssertionError(f"missing native {label} for batch integration")
    def files(self,root):
        char,common=fixture()
        trig,ik=synthetic_tables()
        files={"char":char,"common":common,"trig":trig,"ik":ik}
        paths={}
        for name,data in files.items():
            target=root/(name+".bin")
            target.write_bytes(data)
            paths[name]=target
        return paths
    def call(self,files,**opts):
        return batcher.batch(files["char"],files["common"],None,
                            files["trig"],files["ik"],
                            EXPORTER,SOLVER,UPSTREAM,slots="0-3",**opts)
    def test_two_distinct_character_and_common_animations(self):
        with tempfile.TemporaryDirectory() as directory:
            files=self.files(Path(directory))
            result=self.call(files,max_slots=8,max_frames=4)
            self.assertEqual(result["selected_animations"],2)
            self.assertEqual(result["discovered_unique_animations"],2)
            self.assertEqual(result["duplicate_slots"],1)
            self.assertEqual(result["discovery_skips"],{"empty":1})
            self.assertEqual(result["tested_animations"],2)
            self.assertEqual(result["mismatched_animations"],0,result["results"])
            self.assertEqual(result["skipped_during_execution"],{})
            self.assertTrue(result["pass"],result["results"])
            self.assertGreater(result["components_compared"],100)
            self.assertEqual([r["tested_frames"] for r in result["results"]],[4,4])
            self.assertEqual([r["animation_bank"] for r in result["results"]],[0,99])
    def test_sampling_limit_and_mismatch_accounting(self):
        with tempfile.TemporaryDirectory() as directory:
            files=self.files(Path(directory))
            result=self.call(files,max_slots=1,max_frames=2)
            self.assertEqual(result["selected_animations"],1)
            self.assertEqual(result["results"][0]["tested_frames"],2)
            self.assertEqual(result["tested_animations"],1)
            with patch.object(batcher,"compare",return_value={
                "frames":2,"incomplete_frames":0,"components_compared":162,
                "mismatch_count":3,"first_differences":[{"slot":4,"frame":1}]}):
                result=self.call(files,max_slots=1,max_frames=2)
            self.assertEqual(result["mismatched_animations"],1)
            self.assertFalse(result["pass"])
            self.assertEqual(result["results"][0]["first_differences"][0]["slot"],4)
    def test_missing_spline_is_reported_not_passed(self):
        with tempfile.TemporaryDirectory() as directory:
            files=self.files(Path(directory))
            # Invalid spline length is a hard failure before any C subprocess.
            bad=Path(directory)/"bad.bin"
            bad.write_bytes(b"\0")
            with self.assertRaises(batcher.BatchError):
                batcher.batch(files["char"],files["common"],bad,
                    files["trig"],files["ik"],EXPORTER,SOLVER,UPSTREAM,
                    slots="0-2")
            with self.assertRaises(batcher.BatchError):
                self.call(files,max_slots=0)
            with self.assertRaises(batcher.BatchError):
                self.call(files,max_slots=2,max_frames=0)
            with self.assertRaises(batcher.BatchError):
                batcher.batch(files["char"],None,None,
                    files["trig"],files["ik"],EXPORTER,SOLVER,UPSTREAM,
                    slots="3-10")

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--exporter",type=Path,required=True)
    parser.add_argument("--solver",type=Path,required=True)
    parser.add_argument("--upstream",type=Path,required=True)
    args=parser.parse_args()
    EXPORTER=args.exporter.resolve()
    SOLVER=args.solver.resolve()
    UPSTREAM=args.upstream.resolve()
    unittest.main(argv=[sys.argv[0]],verbosity=2)
