"""Synthetic pose-trace captures; no Namco data or emulator dependency."""
import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
import compare_psx_pose_traces as trace

def fixture(tick=7,fighter=0,mask=trace.BODY_MASK,slot=3,frame=5):
    head=trace.HEADER.pack(trace.MAGIC,1,18,tick,slot,frame,fighter,0,mask)
    joints=[]
    for i in range(18):
        if mask & (1<<i):
            origin=(i*100,-i*2,31)
            rot=(4096,0,0,0,4096,0,0,0,4096)
            joints.append(trace.JOINT.pack(*origin,*rot))
        else:
            joints.append(bytes(trace.JOINT.size))
    return head+b"".join(joints)

class TestOracle(unittest.TestCase):
    def test_binary_stable_layout_and_jsonl(self):
        self.assertEqual(trace.RECORD_BYTES,564)
        rows=trace.records_from_bytes(fixture()+fixture(8,1))
        self.assertEqual(len(rows),2)
        self.assertEqual(rows[0]["tick"],7)
        self.assertEqual(rows[0]["parts"][3]["origin"],[300,-6,31])
        self.assertEqual(rows[0]["parts"][4]["rotation"][0],4096)
        self.assertEqual(rows[1]["fighter"],1)
        expected=trace.validate_jsonl("\n".join(map(json.dumps,rows)))
        report=trace.compare(rows,expected)
        self.assertEqual(report["mismatches"],0)
        self.assertEqual(report["joints_compared"],36)
    def test_difference_reports_joint_and_component(self):
        rows=trace.records_from_bytes(fixture())
        ref=trace.records_from_bytes(fixture())
        ref[0]["parts"][7]["origin"][0]+=2
        report=trace.compare(rows,ref)
        self.assertEqual(report["mismatches"],1)
        self.assertIn("part 7 origin[0]",report["details"][0])
        report=trace.compare(rows,ref,origin_tolerance=2)
        self.assertEqual(report["mismatches"],0)
        ref[0]["parts"][7]["rotation"][6]+=1
        report=trace.compare(rows,ref,origin_tolerance=2)
        self.assertEqual(report["mismatches"],1)
        self.assertIn("rotation[6]",report["details"][0])
        self.assertEqual(trace.compare(rows,ref,2,1)["mismatches"],0)
    def test_frame_loss_move_mismatch_and_incomplete(self):
        a=trace.records_from_bytes(fixture())
        b=trace.records_from_bytes(fixture(frame=9))
        self.assertIn("move mismatch",str(trace.compare(a,b)["details"]))
        b=trace.records_from_bytes(fixture(9))
        result=trace.compare(a,b)
        self.assertEqual(result["frames_compared"],0)
        self.assertTrue(any("missing" in x for x in result["details"]))
        partial=trace.records_from_bytes(fixture(mask=1))
        self.assertEqual(trace.compare(partial,partial)["mismatches"],1)
        self.assertEqual(trace.compare(partial,partial,allow_incomplete=True)["mismatches"],0)
        self.assertIsNone(partial[0]["parts"][2])
    def test_fail_closed_on_corrupt_bytes_and_input(self):
        binary=bytearray(fixture(mask=1))
        for corrupt in (bytes(binary[:-1]),b"",b"aaaa"+bytes(binary[4:])):
            with self.assertRaises(trace.TraceError):
                trace.records_from_bytes(corrupt)
        binary[trace.HEADER.size+trace.JOINT.size+4]=10
        with self.assertRaisesRegex(trace.TraceError,"nonzero payload"):
            trace.records_from_bytes(bytes(binary))
        with self.assertRaises(trace.TraceError):
            trace.validate_jsonl('{"tick":1}')
        row=trace.records_from_bytes(fixture())[0]
        with self.assertRaises(trace.TraceError):
            trace.compare([row,row],[row])
        with self.assertRaises(trace.TraceError):
            trace.compare([row],[row],rotation_tolerance=-1)

if __name__=="__main__":
    unittest.main()
