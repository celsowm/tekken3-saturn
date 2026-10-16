import copy
from pathlib import Path
import sys
import unittest
import struct

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze_psx_phases import analyze,verify_checkpoint,FIELDS,read_fighter,CLOCK_FIELDS
from psx_ram_oracle import RAM_BYTES,FIGHTER_BASE,FIGHTER_STRIDE,GLOBAL_FRAME,GAME_STATE


class PhaseAuditTests(unittest.TestCase):
    def fixture(self):
        rows=[]
        for frame in [700,700,701]:
            for phase,call,hp in [('FightFrame:entry',1,100),
                                  ('HitApply:entry',2,100),
                                  ('HitApply:exit',2,94),
                                  ('GPU::Vsync',0,94),
                                  ('FightFrame:exit',1,94)]:
                rows.append(dict(seq=len(rows),cycles=len(rows)*100,
                                 source_frame=frame,phase=phase,
                                 call=call+len(rows)//5*10,
                                 fighters=[dict(health=140),dict(health=hp)]))
        return rows

    def test_retains_repeated_guest_counters_and_vsync_location(self):
        result=analyze(self.fixture())
        self.assertEqual(result['fight_calls'],3)
        self.assertEqual(result['repeated_source_counter_calls'],1)
        self.assertEqual(result['source_counter_deltas'],{0:1,1:1})
        self.assertEqual(result['vsync_locations'],{'FightFrame':3})
        self.assertIn(dict(function='HitApply',fighter=1,field='health',changes=3),
                      result['changed_fields'])

    def test_rejects_missing_return_and_bad_nesting(self):
        rows=self.fixture()
        with self.assertRaisesRegex(ValueError,'inside'):analyze(rows[:-1])
        rows=copy.deepcopy(rows)
        rows[2]['call']=1
        with self.assertRaisesRegex(ValueError,'unpaired'):analyze(rows)

    def test_rejects_clock_or_sequence_corruption(self):
        rows=self.fixture()
        rows[4]['cycles']=0
        with self.assertRaisesRegex(ValueError,'cycles'):analyze(rows)
        rows=self.fixture()
        rows[3]['seq']=4
        with self.assertRaisesRegex(ValueError,'sequence'):analyze(rows)

    def test_crosschecks_final_return_against_independent_ram(self):
        ram=bytearray(RAM_BYTES)
        struct.pack_into('<I',ram,GAME_STATE-0x80000000,8)
        struct.pack_into('<I',ram,GLOBAL_FRAME-0x80000000,701)
        pair=[]
        for i in range(2):
            pair.append({name:i+2 for name in FIELDS})
            for name,(offset,fmt) in FIELDS.items():
                struct.pack_into('<'+fmt,ram,FIGHTER_BASE+i*FIGHTER_STRIDE+offset-0x80000000,i+2)
        last=dict(phase='FightFrame:exit',source_frame=701,fighters=pair)
        self.assertTrue(verify_checkpoint(ram,last))
        pair[1]['health']=4
        with self.assertRaisesRegex(ValueError,'disagrees'):verify_checkpoint(ram,last)

    def test_clock_fields_and_dynamic_move_hold_frame(self):
        ram=bytearray(RAM_BYTES)
        base=FIGHTER_BASE-0x80000000
        struct.pack_into('<I',ram,base+0x54,0x80010000)
        struct.pack_into('<I',ram,base+0x4C,0x80010000)
        ram[0x1001B]=17
        struct.pack_into('<h',ram,base+0x6A,-1)
        names=(FIELDS|CLOCK_FIELDS|dict(pose_hold_frame=None,root_is_pose=None)).keys()
        result=read_fighter(ram,0,names)
        self.assertEqual(result['pose_hold_frame'],17)
        self.assertEqual(result['root_is_pose'],1)
        self.assertEqual(result['hold_frames'],-1)


if __name__=='__main__':unittest.main()
