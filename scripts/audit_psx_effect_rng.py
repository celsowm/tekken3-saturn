#!/usr/bin/env python3
"""Observe shared RNG callers and live effect objects in a retail fight prefix.

Read-only MIPS instruction hooks; does not stub RNG/effects or change state.
Use the pinned remake research before extending this diagnostic. Output is
private original data and belongs under build/ or assets/imported/.
"""
import argparse
from collections import Counter
from contextlib import closing
from dataclasses import fields
import hashlib
import json
from pathlib import Path
import struct
import sys

from fight_oracle_export import capture_frames, RESEARCH, ROOT
sys.path.insert(0, str(RESEARCH))
from fight_harness import Scenario
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import UC_MIPS_REG_RA, UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A3


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', type=Path, required=True)
    p.add_argument('--start', type=int, required=True)
    p.add_argument('--end', type=int, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--projection', action='store_true',
                   help='also observe original projected quads at the insertion routine')
    args = p.parse_args()
    if not 2 <= args.start <= args.end:
        p.error('require 2 <= start <= end (the first setup frame installs hooks)')
    meta = json.loads((args.case / 'seed.json').read_text())
    exe = RESEARCH.parent.parent / 'work/jp_rev1/exe.bin'
    identity = hashlib.sha256(exe.read_bytes()).hexdigest()
    if meta.get('remake_presentation_fixes') is not False or meta.get('original_exe_sha256') != identity:
        raise ValueError('case must name the current retail executable and restoration policy')
    config = meta['scenario']
    scenario = Scenario(**{f.name: config[f.name] for f in fields(Scenario) if f.name in config})
    script = ROOT / config['pad_script'] if config.get('pad_script') else None
    calls, snapshots, projections, frame = [], [], [], [1]
    with closing(capture_frames(scenario, script, True)) as source:
        for fr, (h, pads, sub, logged) in enumerate(source, 1):
            if fr == 1:
                rand_address = next(a for a, name in h.names.items() if name == 'rand')
                def rand_entry(uc, address, size, data):
                    if args.start <= frame[0] <= args.end:
                        calls.append(dict(frame=frame[0], caller=hex(uc.reg_read(UC_MIPS_REG_RA)),
                                          before=h.u32(0x800A3E80)))
                h.cpu.uc.hook_add(UC_HOOK_CODE, rand_entry, begin=rand_address, end=rand_address)
                if args.projection:
                    def projection_entry(uc, address, size, data):
                        if not args.start <= frame[0] <= args.end:
                            return
                        otz_at = uc.reg_read(UC_MIPS_REG_A0)
                        primitive = uc.reg_read(UC_MIPS_REG_A1)
                        flags_at = uc.reg_read(UC_MIPS_REG_A3)
                        display = h.u32(0x800AE3C4)
                        obj = primitive - 0x50 - display * 0x28
                        xy = struct.unpack('<8h', h.cpu.read(0x1F800000, 16))
                        depth_word = h.u32(otz_at)
                        flag_word = h.u32(flags_at + 12)
                        bucket = (depth_word >> 21) & 1023
                        if flag_word & 0x2000:
                            bucket -= 5
                        accepted = (xy[0] <= 368 and xy[2] >= 0 and
                                    xy[1] <= 468 and xy[5] >= 20 and bucket >= 0)
                        projections.append(dict(frame=frame[0], object=hex(obj),
                            model=hex(h.u32(obj+8)), display_buffer=display,
                            primitive_clut=struct.unpack('<H', h.cpu.read(primitive+14, 2))[0],
                            primitive_uv=list(h.cpu.read(primitive+12, 2)),
                            camera=list(struct.unpack('<8i', h.cpu.read(0x800A8A88, 32))),
                            gte_control=list(h.cpu.gte.c),
                            position=list(struct.unpack('<3i', h.cpu.read(obj+0x28, 12))),
                            xy=[list(xy[i:i+2]) for i in range(0, 8, 2)],
                            depth_word=depth_word, flags_word=flag_word,
                            bucket=bucket, cull_predicate=accepted))
                    h.cpu.uc.hook_add(UC_HOOK_CODE, projection_entry, begin=0x8006E734, end=0x8006E734)
            if args.start <= fr <= args.end:
                # Allocation-ordered circular live list (sentinel prev/next).
                sentinel = 0x800A0D30
                at, seen, objects = h.u32(sentinel + 4), set(), []
                while at != sentinel:
                    if at in seen or len(seen) >= 80 or not 0x80000000 <= at < 0x80200000 - 0xA0:
                        raise ValueError('invalid/cyclic effect list')
                    seen.add(at)
                    raw = bytes(h.cpu.read(at, 0xA0))
                    kind, flags, state = struct.unpack_from('<3B', raw, 12)
                    model_pointer=struct.unpack_from('<I',raw,8)[0]
                    model={0:0,0x8002554C:1,0x8002593C:2,0x8002556C:3,0x800256E0:4,0x80025700:5}.get(model_pointer,-1)
                    pitch,yaw=struct.unpack_from('<hh',raw,0x10)
                    speed=struct.unpack_from('<h',raw,0x18)[0]
                    vx,vy,vz,x,y,z=struct.unpack_from('<6i',raw,0x1C)
                    clut0,clut1=(struct.unpack_from('<H',raw,o)[0] for o in (0x5E,0x86))
                    counters=list(struct.unpack_from('<4i',raw,0x40))
                    objects.append(dict(address=hex(at), type=kind, flags=flags, state=state,
                                        position=list(struct.unpack_from('<3i', raw, 0x28)),
                                        counters=counters,
                                        canonical=[kind,flags,state,model,pitch,yaw,speed,clut0,clut1,x,y,z,vx,vy,vz,*counters]))
                    at = h.u32(at + 4)
                snapshots.append(dict(frame=fr, random=h.u32(0x800A3E80), objects=objects,
                                      type_counts=dict(Counter(o['type'] for o in objects))))
            if fr >= args.end:
                break
            frame[0] = fr + 1
    if len(snapshots) != args.end - args.start + 1:
        raise ValueError('fight ended before requested window')
    report = dict(case=str(args.case), original_exe_sha256=identity, remake_presentation_fixes=False,
                  rand_calls=calls, snapshots=snapshots,
                  projections=projections,
                  counts={str(fr): dict(Counter(c['caller'] for c in calls if c['frame'] == fr))
                          for fr in range(args.start, args.end + 1)},
                  scope='Read-only original instruction hooks and live effect snapshots; no C parity claim')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(frames=len(snapshots), rand_calls=len(calls), output=str(args.out))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
