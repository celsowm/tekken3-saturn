#!/usr/bin/env python3
"""Observe retail fighter and particle OT insertions with restored fighter draw.

The pinned fight harness normally replaces DrawSkinnedPart with a return. This
experiment restores its original JP Rev.1 body just before the chosen window,
then uses read-only instruction hooks to observe actual word stores to the OT.
Earlier fighter draw calls remain stubbed, so this is not a full retail replay.
"""
import argparse
from collections import Counter,defaultdict
from contextlib import closing
from dataclasses import fields
import hashlib
import json
from pathlib import Path
import struct
import sys

from fight_oracle_export import capture_frames,RESEARCH,ROOT
sys.path.insert(0,str(RESEARCH))
from fight_harness import Scenario
import psxcpu
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3,UC_MIPS_REG_0

FIGHTER=0x80036f00
FIGHTER_SIZE=2920
EFFECT=0x8006e734
EFFECT_SIZE=200

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case',type=Path,required=True)
    p.add_argument('--start',type=int,default=221);p.add_argument('--end',type=int,default=225)
    p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    if args.start<2 or args.end<args.start:p.error('invalid frame window')
    meta=json.loads((args.case/'seed.json').read_text());exe=(RESEARCH.parent.parent/'work/jp_rev1/exe.bin').read_bytes()
    digest=hashlib.sha256(exe).hexdigest()
    if meta.get('original_exe_sha256')!=digest or meta.get('remake_presentation_fixes') is not False:
        raise ValueError('case and retail executable do not match')
    config=meta['scenario'];scenario=Scenario(**{f.name:config[f.name] for f in fields(Scenario) if f.name in config})
    script=ROOT/config['pad_script'] if config.get('pad_script') else None
    sites=psxcpu.read_sites(RESEARCH.parent.parent/'work/decomp/SLPS_013.00.cop2.txt',FIGHTER,FIGHTER+FIGHTER_SIZE)
    if not sites:raise ValueError('missing original fighter COP2 listing')
    events=[];calls=[];links=[];draw_calls=[0];current=[0];fighter_ot=[0];effect_ot=[0];serial=[0]
    def inst(address):
        at=address-0x80010000+0x800
        return int.from_bytes(exe[at:at+4],'little')
    with closing(capture_frames(scenario,script,True)) as source:
        for frame,(h,*_) in enumerate(source,1):
            if frame==args.start-1:
                offset=FIGHTER-0x80010000+0x800
                h.cpu.load(FIGHTER,exe[offset:offset+FIGHTER_SIZE],sites)
                def fighter_entry(uc,address,size,data):
                    draw_calls[0]+=1;fighter_ot[0]=uc.reg_read(UC_MIPS_REG_A2)&0x1fffffff
                def effect_entry(uc,address,size,data):
                    effect_ot[0]=uc.reg_read(UC_MIPS_REG_A2)&0x1fffffff
                    depth=h.cpu.u32(uc.reg_read(UC_MIPS_REG_A0));desc=h.cpu.u32(uc.reg_read(UC_MIPS_REG_A3)+12)
                    xy=struct.unpack('<8h',h.cpu.read(0x1f800000,16))
                    bucket=((depth>>21)&1023)-(5 if desc&0x2000 else 0)
                    calls.append(dict(frame=current[0]+1,bucket=bucket,
                        cull_predicate=xy[0]<=368 and xy[2]>=0 and xy[1]<=468 and xy[5]>=20 and bucket>=0,
                        primitive=hex(uc.reg_read(UC_MIPS_REG_A1)&0xffffff)))
                def store_entry(uc,address,size,data):
                    instruction=inst(address);rs=(instruction>>21)&31;rt=(instruction>>16)&31
                    offset=instruction&0xffff;offset=offset if offset<32768 else offset-65536
                    destination=(uc.reg_read(UC_MIPS_REG_0+rs)+offset)&0x1fffffff
                    fighter=FIGHTER<=address<FIGHTER+FIGHTER_SIZE
                    base=fighter_ot[0] if fighter else effect_ot[0]
                    if base<=destination<base+4096:
                        event=dict(frame=current[0]+1,bucket=(destination-base)//4,
                            pc=hex(address),value=hex(uc.reg_read(UC_MIPS_REG_0+rt)&0xffffff),
                            previous=hex(h.cpu.u32(destination)&0xffffff),serial=serial[0])
                        serial[0]+=1
                        if fighter:
                            descriptor=uc.reg_read(UC_MIPS_REG_A0)
                            packed=h.cpu.u32(descriptor);word1=h.cpu.u32(descriptor+4)
                            group={0x800376c8:0,0x800377d4:1,0x800378f8:2,0x80037a3c:3}[address]
                            quad=bool(group&1)
                            flags=word1 if quad else packed
                            slots=[(packed>>2)&0x7f,(packed>>9)&0x7f,(packed>>16)&0x7f]
                            slots.append((packed>>25)&0x7f if quad else slots[2])
                            depths=[struct.unpack('<H',h.cpu.read(0x1f8001b8+2*i,2))[0] for i in slots]
                            z0=struct.unpack('<H',h.cpu.read(0x1f8001b8+((packed&0x1fc)>>1),2))[0]
                            end_offset=(packed>>24) if quad else ((packed>>14&0x1fc)>>1)
                            zend=struct.unpack('<H',h.cpu.read(0x1f8001b8+end_offset,2))[0]
                            bias=flags>>25
                            if bias&64:bias-=128
                            mac0=h.cpu.gte.read_data(24)
                            if mac0&0x80000000:mac0-=0x100000000
                            if flags&0x01000000 and mac0<=0:bias=-bias
                            predicted=((z0+zend-bias)&0x7fe)>>1
                            packet=0x80000000|(uc.reg_read(UC_MIPS_REG_0+rt)&0xffffff)
                            offsets=((8,16,24),(8,16,24,32),(8,20,32),(8,20,32,44))[group]
                            packet_xy=[struct.unpack('<hh',h.cpu.read(packet+off,4)) for off in offsets]
                            scratch_xy=[struct.unpack('<hh',h.cpu.read(0x1f800000+4*i,4)) for i in slots[:len(offsets)]]
                            if packet_xy!=scratch_xy:
                                raise AssertionError(f'fighter face packet vertices differ from selected scratch slots: {event}')
                            event.update(first_depth_bucket=z0,last_depth_bucket=zend,bias=bias,descriptor=flags,
                                         packed=packed,word1=word1,group=group,slots=slots,slot_depths=depths,
                                         packet_xy=packet_xy,nclip=mac0,predicted_bucket=predicted,quad=quad)
                            if predicted!=event['bucket']:
                                raise AssertionError(f'fighter OT formula mismatch {event}')
                        (events if fighter else links).append(event)
                h.cpu.uc.hook_add(UC_HOOK_CODE,fighter_entry,begin=FIGHTER,end=FIGHTER)
                h.cpu.uc.hook_add(UC_HOOK_CODE,effect_entry,begin=EFFECT,end=EFFECT)
                for begin,size in ((FIGHTER,FIGHTER_SIZE),(EFFECT,EFFECT_SIZE)):
                    for address in range(begin,begin+size,4):
                        if inst(address)>>26==0x2b:
                            h.cpu.uc.hook_add(UC_HOOK_CODE,store_entry,begin=address,end=address)
            current[0]=frame
            if frame>=args.end:break
    per_frame=[]
    for frame in range(args.start,args.end+1):
        face=[e for e in events if e['frame']==frame];particles=[e for e in links if e['frame']==frame]
        by_bucket=defaultdict(set)
        for e in face:by_bucket[e['bucket']].add(e['value'])
        same=[e for e in particles if e['bucket'] in by_bucket]
        direct=[e for e in same if e['previous'] in by_bucket[e['bucket']]]
        per_frame.append(dict(frame=frame,fighter_links=len(face),particle_links=len(particles),
                              same_bucket=len(same),direct_fighter_predecessor=len(direct)))
    report=dict(original_exe_sha256=digest,case=str(args.case),frame_window=[args.start,args.end],
                restored_fighter_cop2_sites=len(sites),draw_calls=draw_calls[0],
                fighter_store_pcs=dict(Counter(e['pc'] for e in events)),per_frame=per_frame,
                fighter_events=events,particle_calls=calls,particle_links=links,
                scope='Retail instructions in harness with fighter draw restored only for this window; no GPU pixels or full-run proof')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('frame_window','restored_fighter_cop2_sites','draw_calls','fighter_store_pcs','per_frame')}))
    return 0
if __name__=='__main__':raise SystemExit(main())
