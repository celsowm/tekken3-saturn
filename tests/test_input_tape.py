import importlib.util,json,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('build_input_tape',ROOT/'scripts/build_input_tape.py')
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
class InputTapeTests(unittest.TestCase):
    def test_replay_tick_boundaries_and_reset(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'tape.json';header=root/'t3_private_input_tape.h'
            source.write_text(json.dumps(dict(events=[[0,0,0,0,0],[4,32,32,1,1],[5,32,0,1,0],[9,0,0,0,0]])))
            mod.build(source,header)
            test=root/'test.c';test.write_text('''#include "input_tape.h"
#include <assert.h>
int main(void) {
 t3_input_t a={7,7},b={7,7};t3_input_tape_reset();
 t3_input_tape_step(3,&a,&b);assert(!a.held && !b.held);
 t3_input_tape_step(4,&a,&b);assert(a.held==32 && a.pressed==32 && b.held==1 && b.pressed==1);
 t3_input_tape_step(5,&a,&b);assert(a.held==32 && !a.pressed && b.held==1 && !b.pressed);
 t3_input_tape_step(9,&a,&b);assert(!a.held && !b.held);
 t3_input_tape_step(40,&a,&b);assert(!a.pressed && !b.pressed);
 t3_input_tape_reset();t3_input_tape_step(0,&a,&b);assert(!a.held && !b.held);
 return 0;
}''')
            binary=root/'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DT3_REPLAY_INPUT_TAPE=1',
                '-I'+str(ROOT/'src/game'),'-I'+str(ROOT/'src/saturn'),'-I'+str(root),
                str(test),str(ROOT/'src/saturn/input_tape.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
    def test_rejects_malformed_or_unordered_inputs(self):
        for events in ([],[[1,0,0,0,0]],[[0,0,0,0,0],[0,1,0,0,0]],
                       [[0,0,0,0,0],[2,4096,0,0,0]],[[0,0,0,0,0],[65536,0,0,0,0]],[[0,False,0,0,0]]):
            with self.subTest(events=events),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);source=root/'tape.json';source.write_text(json.dumps(dict(events=events)))
                with self.assertRaises(ValueError):mod.build(source,root/'out.h')
                self.assertFalse((root/'out.h').exists())
