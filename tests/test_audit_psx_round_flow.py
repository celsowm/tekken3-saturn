import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from audit_psx_round_flow import actor,audit
from audit_psx_round_score import Ranges
from extract_psx_pose_tables import mapped
from prepare_psx_phase_capture import round_descriptors
from psx_disc_import import US_EXE_SHA
RUNNER=None


class RoundFlowAuditTests(unittest.TestCase):
    def test_stance_fallback_follows_original_pointer_chain(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);_,rows,_=self.fixture(p)
            ram=bytearray((p/'round-2-before.ranges').read_bytes()[20:])
            row=copy.deepcopy(rows[1]['fighters'][0]);row['slot']=21
            struct.pack_into('<H',ram,0xA9228+0xA0,21)
            struct.pack_into('<H',ram,0x1F0000+16,22)
            struct.pack_into('<I',ram,0xADC20,0x801D0000)
            struct.pack_into('<I',ram,0x1D0000+12,0x801D0100)
            struct.pack_into('<I',ram,0x1D0100+22*4,0x801D0300)
            struct.pack_into('<I',ram,0x1D0300+4,0x204)
            raw=b'T3RANGES'+struct.pack('<III',1,0x80000000,len(ram))+ram
            result=actor(Ranges(raw),0,row,0x800ADC20,dict(state=2,result=1))
            self.assertEqual(result['state_word'],0x204)
            self.assertIsNone(actor(Ranges(raw),0,row)['state_word'])

    def fixture(self,path):
        layout_path=ROOT/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json'
        layout=json.loads(layout_path.read_text())['blocks']['exe'];points=dict(layout['points'])
        fields=round_descriptors(lambda a:points.get(a) or mapped(layout,a,4),True)
        observer=b'synthetic observer\n';(path/'observer.lua').write_bytes(observer)
        meta=dict(round_flow_capture=True,exe_sha256=US_EXE_SHA,
            layout_sha256=hashlib.sha256(layout_path.read_bytes()).hexdigest(),round_fields=fields,
            observer_sha256=hashlib.sha256(observer).hexdigest())
        (path/'provenance.json').write_text(json.dumps(meta))
        rows=[]
        for seq,phase in enumerate(['FightFrame:entry','RoundFlow:entry','RoundFlow:exit','FightFrame:exit']):
            after=seq>=2;ram=bytearray(0x200000)
            struct.pack_into('<I',ram,0xAFA88,1);struct.pack_into('<I',ram,0xAE204,8)
            values={name:0 for name in fields}
            values.update(state=1,played=1,timer=9 if after else 10,frame=101 if after else 100,
                to_win=2,max_rounds=3,human_mask=3,draw_wins=1,camera=2,input0=1,input1=1,
                replay_mode=7,frame_counter=700)
            for name,f in fields.items():
                start=f['address']-0x80000000
                ram[start:start+f['size']]=values[name].to_bytes(f['size'],'little',signed=f['signed'])
            pair=[]
            for i in range(2):
                b=0xA9228+i*0x188C;pose=0x801F0000+i*0x100
                for off,v in [(0x3F4,140*65536),(0x3F8,140*65536),(0x54,pose)]:struct.pack_into('<I',ram,b+off,v)
                for off,v in [(0xA0,3),(0x58,1)]:struct.pack_into('<h',ram,b+off,v)
                ram[b+0xC3]=1;ram[pose-0x80000000+0x18]=54
                struct.pack_into('<H',ram,pose-0x80000000+16,3)
                pair.append(dict(slot=3,pose=1,health=140*65536,round_wins=0,round_won=0,
                                 win_pose=0,health_left=0,active=1,pose_length=54))
            row=dict(seq=seq,cycles=seq*100,source_frame=700,phase=phase,
                call=2 if 'RoundFlow' in phase else 1,round=values,fighters=pair)
            rows.append(row)
            if 'RoundFlow' in phase:
                blob=b'T3RANGES'+struct.pack('<III',1,0x80000000,len(ram))+ram
                (path/('round-2-'+('after' if after else 'before')+'.ranges')).write_bytes(blob)
            if seq==3:
                (path/'paused-ram.bin').write_bytes(ram)
                (path/'paused-phase.jsonl').write_text(json.dumps(row))
        (path/'phases.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in rows))
        (path/'execution.json').write_text(json.dumps(dict(running=False)))
        return layout_path,rows,fields

    def test_native_pair_and_independent_expected_result(self):
        if RUNNER is None:self.skipTest('pass --runner for native integration')
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);layout,rows,fields=self.fixture(p)
            report=audit(p,RUNNER,layout)
            self.assertTrue(report['all_equal']);self.assertEqual(report['paired_calls'],1)
            # Alter all three sources consistently: RAM/JSON validation passes,
            # then the native timer comparison must reject the false result.
            off=fields['timer']['address']-0x80000000
            raw=bytearray((p/'round-2-after.ranges').read_bytes())
            struct.pack_into('<i',raw,20+off,8);(p/'round-2-after.ranges').write_bytes(raw)
            ram=bytearray((p/'paused-ram.bin').read_bytes());struct.pack_into('<i',ram,off,8)
            (p/'paused-ram.bin').write_bytes(ram)
            for row in rows[2:]:row['round']['timer']=8
            (p/'phases.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in rows))
            (p/'paused-phase.jsonl').write_text(json.dumps(rows[-1]))
            report=audit(p,RUNNER,layout)
            self.assertFalse(report['all_equal'])
            self.assertEqual(report['differences'][0]['fields']['round.timer'],[9,8])

    def test_requires_paused_correct_descriptors_and_final_observation(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);layout,rows,fields=self.fixture(p)
            (p/'execution.json').write_text('{"running":true}')
            with self.assertRaisesRegex(ValueError,'paused'):audit(p,Path('unused'),layout)
            (p/'execution.json').write_text('{"running":false}')
            row=copy.deepcopy(rows[-1]);row['source_frame']=701
            (p/'paused-phase.jsonl').write_text(json.dumps(row))
            with self.assertRaisesRegex(ValueError,'final trace'):audit(p,Path('unused'),layout)
            meta=json.loads((p/'provenance.json').read_text())
            meta['round_fields']['physical1']['address']+=2
            (p/'provenance.json').write_text(json.dumps(meta))
            with self.assertRaisesRegex(ValueError,'descriptors'):audit(p,Path('unused'),layout)

    def test_entry_ram_and_json_must_agree(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);layout,rows,_=self.fixture(p)
            rows[1]['fighters'][0]['pose']=2
            (p/'phases.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in rows))
            with self.assertRaisesRegex(ValueError,'JSON/RAM'):audit(p,Path('unused'),layout)


if __name__=='__main__':
    if '--runner' in sys.argv:
        index=sys.argv.index('--runner');RUNNER=Path(sys.argv[index+1]).resolve();del sys.argv[index:index+2]
    unittest.main()
