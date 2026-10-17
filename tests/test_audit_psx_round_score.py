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
from audit_psx_round_score import Ranges,audit,checked_state
from extract_psx_pose_tables import mapped
from prepare_psx_phase_capture import round_descriptors
from psx_disc_import import US_EXE_SHA
from psx_ram_oracle import FIGHTER_BASE,FIGHTER_STRIDE
RUNNER=None


def blob(ranges):
    return b'T3RANGES'+struct.pack('<I',len(ranges))+b''.join(struct.pack('<II',a,len(b))+b for a,b in ranges)


class RoundScoreAuditTests(unittest.TestCase):
    def test_compact_reader_rejects_truncation_trailing_and_conflicts(self):
        raw=blob([(0x80000000,b'\xff\xff')])
        self.assertEqual(Ranges(raw).read(0x80000000,2,True),-1)
        for bad in [raw[:-1],raw+b'\0',blob([(0x801FFFFF,b'ab')]),
                    blob([(0x80000000,b'ab'),(0x80000001,b'c')])]:
            with self.assertRaises(ValueError):Ranges(bad)
        with self.assertRaisesRegex(ValueError,'uncaptured'):Ranges(raw).read(0x80000002,2)

    def fixture(self,path):
        layout_path=ROOT/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json'
        layout=json.loads(layout_path.read_text())['blocks']['exe'];points=dict(layout['points'])
        fields=round_descriptors(lambda a:points.get(a) or mapped(layout,a,4))
        provenance=dict(exe_sha256=US_EXE_SHA,layout_sha256=hashlib.sha256(layout_path.read_bytes()).hexdigest(),round_fields=fields)
        (path/'provenance.json').write_text(json.dumps(provenance))
        rows=[]
        for seq,phase in enumerate(['FightFrame:entry','RoundWins:entry','RoundWins:exit','FightFrame:exit']):
            after=seq>=2
            values={k:0 for k in fields}
            values.update(played=1,to_win=2,max_rounds=2 if after else 3,human_mask=3,draw_wins=1)
            pair=[dict(round_wins=int(after),round_won=-1,health_left=4096) for _ in range(2)]
            row=dict(seq=seq,cycles=seq*100,source_frame=700,phase=phase,call=2 if 'RoundWins' in phase else 1,round=values,fighters=pair)
            rows.append(row)
            if 'RoundWins' in phase:
                ram=bytearray(0x200000)
                struct.pack_into('<I',ram,0xAFA88,1)
                for name,f in fields.items():
                    start=f['address']-0x80000000
                    ram[start:start+f['size']]=values[name].to_bytes(f['size'],'little',signed=f['signed'])
                for i,fighter in enumerate(pair):
                    for name,offset in [('round_wins',0x44),('round_won',0x46),('health_left',0x400)]:
                        struct.pack_into('<h',ram,FIGHTER_BASE-0x80000000+i*FIGHTER_STRIDE+offset,fighter[name])
                (path/('round-2-'+('after' if after else 'before')+'.ranges')).write_bytes(blob([(0x80000000,ram)]))
        (path/'phases.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows))
        return layout_path,rows,fields

    def test_checks_independent_fighter_ram_and_vs_mode(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);_,rows,fields=self.fixture(p)
            raw=(p/'round-2-before.ranges').read_bytes()
            checked_state(rows[1],raw,fields)
            row=copy.deepcopy(rows[1]);row['fighters'][0]['round_wins']=1
            with self.assertRaisesRegex(ValueError,'JSON/RAM'):checked_state(row,raw,fields)
            ram=bytearray(raw);struct.pack_into('<I',ram,20+0xAFA88,8)
            with self.assertRaisesRegex(ValueError,'normal VS'):checked_state(rows[1],ram,fields)

    def test_native_pair_and_fail_closed_on_wrong_output(self):
        if RUNNER is None:self.skipTest('pass --runner for native integration')
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);layout,rows,fields=self.fixture(p)
            result=audit(p,RUNNER,layout)
            self.assertEqual(result['paired_rounds'],1)
            self.assertTrue(result['all_equal'])
            raw=bytearray((p/'round-2-after.ranges').read_bytes())
            struct.pack_into('<h',raw,20+FIGHTER_BASE-0x80000000+0x44,2)
            (p/'round-2-after.ranges').write_bytes(raw)
            rows[2]['fighters'][0]['round_wins']=2
            (p/'phases.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows))
            with self.assertRaisesRegex(ValueError,'native/original'):audit(p,RUNNER,layout)


if __name__=='__main__':
    if '--runner' in sys.argv:
        at=sys.argv.index('--runner');RUNNER=Path(sys.argv[at+1]).resolve();del sys.argv[at:at+2]
    unittest.main()
