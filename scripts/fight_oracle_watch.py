#!/usr/bin/env python3
"""Report which original code writes an address during frames of an oracle case.

Replays the scenario of a fight_oracle_export.py case (its seed.json) in the
Tekken3Remake Unicorn harness (third_party/Tekken3Remake/tools/research,
PolyForm Noncommercial) and, for the harness frames FIRST..LAST (the N of
frame-N.ranges), prints every write to the watched Japan Rev.1 addresses with
the writing PC, the return address and the value. Fighter fields can be named
as fighter:offset (fighter 0 or 1, Rev.1 record bases). Stores made by the
harness's software GTE (SWC2) bypass the write hook: --at PC prints the
watched values each time PC runs instead.

Prints only addresses and values; nothing is written to disk. Run with
third_party/Tekken3Remake/work/venv/bin/python.

  fight_oracle_watch.py build/oracle-paul-s3 4462 4463 --watch 0:0x1278:2 --watch 1:0x1278:2
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'


def parse_watch(text, fighters):
    parts = text.split(':')
    if len(parts) == 3:
        base = fighters[int(parts[0])]
        return base + int(parts[1], 0), int(parts[2], 0)
    if len(parts) == 2:
        return int(parts[0], 0), int(parts[1], 0)
    raise SystemExit(f'bad --watch {text!r}: use ADDRESS:SIZE or FIGHTER:OFFSET:SIZE')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('case', type=Path)
    p.add_argument('first', type=int)
    p.add_argument('last', type=int)
    p.add_argument('--watch', action='append', required=True)
    p.add_argument('--at', action='append', default=[], help='print the watched values when this PC runs')
    args = p.parse_args()
    sys.path.insert(0, str(RESEARCH))
    import fight_harness
    from unicorn import UC_HOOK_MEM_WRITE, UC_HOOK_CODE
    from unicorn.mips_const import UC_MIPS_REG_PC, UC_MIPS_REG_RA
    s = json.loads((args.case / 'seed.json').read_text())['scenario']
    scenario = fight_harness.Scenario(s['name'], tuple(s['chars']), tuple(s['costumes']), s['stage'],
                                      seed=s['seed'], styles=tuple(s['styles']), round_time=s['round_time'],
                                      rounds=s['rounds'], max_frames=s['max_frames'])
    watches = [parse_watch(w, fight_harness.FIGHTERS) for w in args.watch]
    # Frames yielded so far; a write belongs to the frame being run (+1).
    state = {'frame': 0}

    def on_write(uc, _access, address, size, value, _data):
        print(f'frame {state["frame"] + 1} write {address:#010x}/{size} = {value & (1 << 8 * size) - 1:#x} '
              f'pc {uc.reg_read(UC_MIPS_REG_PC):#010x} ra {uc.reg_read(UC_MIPS_REG_RA):#010x}')

    def on_code(uc, address, _size, _data):
        values = ' '.join(f'{a:#010x}={uc.mem_read(a & 0x1FFFFFFF, n).hex()}' for a, n in watches)
        print(f'frame {state["frame"] + 1} at {address:#010x} ra {uc.reg_read(UC_MIPS_REG_RA):#010x}: {values}')

    hooked = False
    for harness, _pads, _sub, _calls in fight_harness.record(scenario):
        state['frame'] += 1
        if hooked:
            print(f'-- after frame {state["frame"]}: FightMain sub-state {harness.s32(fight_harness.SUB_STATE)}')
        if state['frame'] >= args.last:
            break
        if not hooked and state['frame'] + 1 >= args.first:
            # Unicorn may report a KSEG0 store by its physical address.
            for address, size in watches:
                for at in {address, address & 0x1FFFFFFF}:
                    harness.cpu.uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=at, end=at + size - 1)
            for pc in args.at:
                harness.cpu.uc.hook_add(UC_HOOK_CODE, on_code, begin=int(pc, 0), end=int(pc, 0))
            hooked = True


if __name__ == '__main__':
    main()
