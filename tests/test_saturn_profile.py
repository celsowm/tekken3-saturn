"""CPU profile attribution checks with synthetic ELF symbol ranges."""
import json,sys,tempfile,unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze_saturn_profile import analyze,attribute,completed_calls,read_symbols

class ProfileAttribution(unittest.TestCase):
    def test_weak_inline_functions_keep_their_measured_cost(self):
        # GCC outlines C++ inline kernels into weak text symbols. Ignoring W
        # would hide projection/visibility cost instead of identifying it.
        with patch('analyze_saturn_profile.subprocess.check_output',return_value=
                '06004000 00000020 W render::project(int const*)\n06004020 00000010 T _main\n'):
            self.assertEqual(read_symbols('unused','nm'),[
                (0x6004000,32,'render::project(int const*)'),(0x6004020,16,'_main')])
    def test_aliases_cache_mirrors_and_interrupt_costs(self):
        symbols=[(0x6004000,16,'_function'),(0x6004004,0,'_alias'),
                 (0x6004010,0,'_asm'),(0x6004014,0,'_asm_tail'),(0x6004018,8,'_next')]
        rows=[dict(cpu='master',pc=str(0x26004006),instructions='2',execution_cycles='7'),
              dict(cpu='master',pc=str(0x6004012),instructions='1',execution_cycles='3'),
              dict(cpu='slave',pc=str(0x6004018),instructions='1',execution_cycles='5')]
        totals={'master':dict(instructions=3,execution_cycles=21,interrupt_entry_cycles=11),
                'slave':dict(instructions=1,execution_cycles=5,interrupt_entry_cycles=0)}
        a=attribute(rows,symbols,totals)
        self.assertEqual(a['master']['functions'][0]['symbol'],'_function')
        self.assertEqual(a['master']['functions'][0]['boundary'],'elf_size')
        self.assertEqual(a['master']['functions'][1]['boundary'],'inferred_next_text_symbol')
        self.assertEqual(a['slave']['functions'][0]['symbol'],'_next')

    def test_missing_histogram_data_cannot_pass(self):
        totals={c:dict(instructions=1,execution_cycles=3,interrupt_entry_cycles=0) for c in ('master','slave')}
        with self.assertRaisesRegex(ValueError,'reconcile'):attribute([],[],totals)

    def test_inclusive_calls_keep_cpu_clocks_and_nested_costs_separate(self):
        def row(cpu,event,pc,target,cost,frame=7):
            return dict(cpu=cpu,event=event,pc=pc,target=target,
                        execution_cycles_before_step=cost,frame=frame)
        rows=[row('master','return',0x500,0x104,1), # left trace boundary
              row('master','call',0x100,0x200,10),
              row('slave','call',0x100,0x200,100),
              row('master','call',0x210,0x300,20),
              row('master','return',0x330,0x214,40),
              row('master','return',0x240,0x104,60,8),
              row('slave','return',0x240,0x104,190,8),
              row('master','call',0x100,0x200,70,8)] # right trace boundary
        out=completed_calls(rows,[(0x200,64,'outer'),(0x300,64,'inner')],['outer'])
        master=out['master'];outer=master['functions'][0]
        self.assertEqual((outer['symbol'],outer['calls'],outer['inclusive_cpu_cost']),('outer',1,50))
        self.assertEqual(master['functions'][1]['inclusive_cpu_cost'],20)
        self.assertEqual(master['open_calls_at_end'],1)
        self.assertEqual(master['unmatched_returns'],1)
        self.assertEqual(master['selected_intervals'][0]['last_field'],8)
        self.assertEqual(out['slave']['functions'][0]['inclusive_cpu_cost'],90)

    def test_entry_jumps_do_not_pop_callers_and_nonlocal_returns_are_counted(self):
        def row(event,pc,target,cost):
            return dict(cpu='master',event=event,pc=pc,target=target,
                        execution_cycles_before_step=cost,frame=1)
        rows=[row('call',0x20000100,0x20000200,1),
              row('return',0x350,0x444,2), # uncalled helper entry/return
              row('call',0x210,0x300,3),
              row('return',0x240,0x20000104,11)]
        out=completed_calls(rows,[(0x200,64,'outer'),(0x300,64,'abandoned')])['master']
        self.assertEqual(out['unmatched_returns'],1)
        self.assertEqual(out['calls_skipped_by_return'],1)
        self.assertEqual([s['symbol'] for s in out['functions']],['outer'])
        self.assertEqual(out['functions'][0]['inclusive_cpu_cost'],10)
        self.assertEqual(out['open_calls_at_end'],0)

    def test_event_cost_rollbacks_and_invalid_cpus_are_rejected(self):
        row=dict(cpu='master',event='interrupt',pc=0,target=0,
                 execution_cycles_before_step=20,frame=1)
        with self.assertRaisesRegex(ValueError,'non-monotonic'):
            completed_calls([row,dict(row,execution_cycles_before_step=19)],[])
        with self.assertRaisesRegex(ValueError,'CPU'):
            completed_calls([dict(row,cpu='other')],[])

    def test_recursive_calls_with_the_same_return_pc_complete_in_stack_order(self):
        def row(event,pc,target,cost):
            return dict(cpu='master',event=event,pc=pc,target=target,
                        execution_cycles_before_step=cost,frame=1)
        rows=[row('call',0x100,0x200,0),row('call',0x210,0x200,10),
              row('call',0x210,0x200,20),row('return',0x230,0x214,30),
              row('return',0x230,0x214,50),row('return',0x230,0x104,80)]
        out=completed_calls(rows,[(0x200,64,'recursive')],['recursive'])['master']
        self.assertEqual([r['cpu_cost'] for r in out['selected_intervals']],[10,40,80])
        self.assertEqual(out['functions'][0]['median_cpu_cost'],40)
        self.assertEqual(out['open_calls_at_end'],0)

    def test_saved_profile_event_loss_cannot_produce_a_call_report(self):
        # The metadata can be intact while its CSV is partially copied. Check
        # every event class, including events which do not change the stack.
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);prefix=root/'debug';elf=root/'app.elf';elf.write_bytes(b'fixture')
            cpu=dict(instructions=0,execution_cycles=0,interrupt_entry_cycles=0,
                     calls=0,returns=0,interrupts=0,divisions=0,dma_transfers=0)
            meta=dict(version=1,truncated=False,frames_stored=1,trace_frames_requested=1,
                      master=dict(cpu),slave=dict(cpu))
            Path(str(prefix)+'.pc.csv').write_text('cpu,pc,instructions,execution_cycles\n')
            Path(str(prefix)+'.events.csv').write_text(
                'cpu,frame,event,pc,target,execution_cycles_before_step\n')
            def run():
                Path(str(prefix)+'.json').write_text(json.dumps(meta))
                with patch('analyze_saturn_profile.read_symbols',return_value=[]):
                    return analyze(prefix,elf,'unused')
            self.assertEqual(run()['completed_calls']['master']['functions'],[])
            for key in ('calls','returns','interrupts','divisions','dma_transfers'):
                meta['master'][key]=1
                with self.assertRaisesRegex(ValueError,'event counts'):run()
                meta['master'][key]=0
            meta['frames_stored']=0
            with self.assertRaisesRegex(ValueError,'fields'):run()
            meta['frames_stored']=1
            meta['slave']['dropped_snapshots']=1
            with self.assertRaisesRegex(ValueError,'incomplete'):run()

    def test_unknown_events_and_field_rollbacks_are_rejected(self):
        row=dict(cpu='master',event='interrupt',pc=0,target=0,
                 execution_cycles_before_step=20,frame=2)
        with self.assertRaisesRegex(ValueError,'fields'):
            completed_calls([row,dict(row,frame=1)],[])
        with self.assertRaisesRegex(ValueError,'unsupported'):
            completed_calls([dict(row,event='unknown')],[])

if __name__=='__main__':unittest.main()
