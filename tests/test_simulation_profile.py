import json,re,struct,sys,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze_simulation_profile import STAGES,CAPACITY,decode,summarize,IncompleteProfileError
from run_simulation_profile import run

class ProfileTests(unittest.TestCase):
    def test_stage_ids_match_the_native_wire_contract(self):
        header=(Path(__file__).resolve().parents[1]/'src/game/sim_profile.h').read_text()
        native=re.search(r'enum\s*\{([^}]+)\}',header).group(1)
        aliases={'branch_decode':'branch_rows','command':'command_calls'}
        names=[name.strip().removeprefix('T3_SIM_').lower() for name in native.split(',')]
        self.assertEqual(tuple(aliases.get(name,name) for name in names[:-1]),STAGES)
        self.assertEqual(names[-1],'stages')
    def fixture(self,count=2):
        n=len(STAGES)*3;words=7+n*3
        raw=bytearray(4*(11+3*n+CAPACITY*words))
        struct.pack_into('>11I',raw,0,0x54335350,1,1000,count,count%CAPACITY,2*count,
                         200,len(STAGES),3,CAPACITY,0)
        values=[0]*(n*3);root=2*len(STAGES)
        def counter(i,inc,exc,calls):values[i*3:i*3+3]=[inc,exc,calls]
        counter(root,20,8,1);counter(root+STAGES.index('branch'),12,9,2)
        counter(STAGES.index('fk'),3,3,1)
        counter(root+STAGES.index('command_calls'),0,0,10)
        struct.pack_into('>'+str(n*3)+'I',raw,44,*[v*count for v in values])
        for tick in range(max(0,count-CAPACITY),count):
            at=44+12*n+4*words*(tick%CAPACITY)
            struct.pack_into('>7I',raw,at,200+tick//3,tick,1,7,3,0,0)
            struct.pack_into('>'+str(n*3)+'I',raw,at+28,*values)
        return raw
    def test_wrap_aggregate_tail_and_exclusive_reconciliation(self):
        r=summarize(decode(self.fixture(CAPACITY+5)))
        self.assertEqual(r['aggregate_ticks'],CAPACITY+5)
        self.assertEqual(r['retained_ticks'],CAPACITY)
        self.assertEqual(r['stages']['tick']['inclusive_ms_per_tick'],20)
        self.assertEqual(r['stages']['branch']['exclusive_ms_per_tick'],9)
        self.assertEqual(r['stages']['fk']['actors']['p1']['calls'],CAPACITY+5)
        self.assertEqual(r['stages']['command_calls']['kind'],'event')
        self.assertEqual(sum(v['exclusive_ms_per_tick'] for v in r['stages'].values()),20)
    def test_torn_publication_is_distinct_from_runtime_failure(self):
        raw=self.fixture();struct.pack_into('>I',raw,20,5)
        with self.assertRaises(IncompleteProfileError):decode(raw)
        struct.pack_into('>I',raw,40,8)
        with self.assertRaisesRegex(ValueError,'guest profiler error'):decode(raw)
    def test_reject_schema_corruption_open_stack_and_bad_totals(self):
        for offset,value in [(0,0),(16,1),(20,8),(40,8),(44,123)]:
            raw=self.fixture();struct.pack_into('>I',raw,offset,value)
            with self.assertRaises(ValueError):decode(raw)
        raw=self.fixture()
        with self.assertRaises(ValueError):decode(raw[:-1])
        row=44+12*len(STAGES)*3
        struct.pack_into('>I',raw,row+24,1)
        with self.assertRaisesRegex(ValueError,'unclosed'):decode(raw)
    def test_event_count_cannot_claim_timing(self):
        raw=self.fixture();index=2*len(STAGES)+STAGES.index('command_calls')
        struct.pack_into('>I',raw,44+12*index,1)
        with self.assertRaisesRegex(ValueError,'event counters'):decode(raw)
    def test_capture_retries_only_torn_publication(self):
        with tempfile.TemporaryDirectory() as tmp:
            args=SimpleNamespace(out=Path(tmp)/'capture',frames=600,dump_symbol=[])
            with patch('run_simulation_profile.saturn_probe.run',return_value={'elf_sha256':'fixture'}) as probe,\
                 patch('run_simulation_profile.analyze',side_effect=[IncompleteProfileError(),{'aggregate_ticks':2}]):
                report=run(args)
            self.assertEqual([c.args[0].frames for c in probe.call_args_list],[600,601])
            self.assertEqual(report['publication_retries'],1)
            self.assertEqual(json.loads((args.out/'profile.json').read_text())['actual_fields'],601)
        with tempfile.TemporaryDirectory() as tmp:
            args=SimpleNamespace(out=Path(tmp)/'capture',frames=600,dump_symbol=[])
            with patch('run_simulation_profile.saturn_probe.run',side_effect=ValueError('native panic')) as probe:
                with self.assertRaisesRegex(ValueError,'native panic'):run(args)
                self.assertEqual(probe.call_count,1)

if __name__=='__main__':unittest.main()
