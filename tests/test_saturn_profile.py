"""CPU profile attribution checks with synthetic ELF symbol ranges."""
import sys,unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze_saturn_profile import attribute,read_symbols

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

if __name__=='__main__':unittest.main()
