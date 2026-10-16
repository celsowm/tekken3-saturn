#!/usr/bin/env python3
"""Embed PRIVATE compact DIVMOT banks into a local Saturn diagnostic build.

Never use original Namco banks or this generated C in public CI/releases.
"""
import argparse
from pathlib import Path
import json
import struct
from divmot_to_logic import sections_of
from extract_psx_sequences import MAX_STEPS, TABLE_LENGTHS

def load_sequences(path):
    if not path:return []
    report=json.loads(path.read_text(encoding="utf8"))
    records=report.get("sequences")
    commands=[first+i for first,n in TABLE_LENGTHS for i in range(n)]
    if report.get("schema")!=1 or not isinstance(records,list) or len(records)!=104:
        raise ValueError("input sequence manifest requires exactly 104 entries")
    cleaned=[]
    for row,command in zip(records,commands):
        if row.get("command")!=command:raise ValueError("wrong command ID/order")
        steps=row.get("steps")
        window=row.get("window")
        if (not isinstance(window,int) or not 1<=window<=60 or
            not isinstance(steps,list) or not 1<=len(steps)<=MAX_STEPS or
            not all(isinstance(step,int) and 0<step<=0x1F09 and
                    not (step&0x00F0) and (step&15)<=9 and (step>>8)<=0x1F
                    for step in steps)):
            raise ValueError(f"invalid PS1 sequence {command:#x}")
        cleaned.append((command,window,steps))
    return cleaned

def generate(bank_path,common_path,out,sequences_path=None,spline_path=None,trig_path=None,ik_path=None,common_branches_path=None,ground_path=None,body_path=None,reactions_path=None,rsqrt_path=None):
    bank=bank_path.read_bytes()
    _,section=sections_of(bank)
    if bank[1]==99:raise ValueError("main bank must be character bank, not type 99")
    common=common_path.read_bytes() if common_path else b""
    if common:
        sections_of(common)
        if common[1]!=99:raise ValueError("common bank must be divmot99")
    # Measured Paul + shared motion/hit/event banks occupy 543,988 bytes.
    # The complete private executable still has to pass the linker's RAM
    # regions and native-boot initialized-image limit; this is an input cap.
    if len(bank)+len(common)>600000:
        raise ValueError("logic banks exceed 600-kilobyte experimental budget")
    sequences=load_sequences(sequences_path)
    spline=spline_path.read_bytes() if spline_path else b""
    trig=trig_path.read_bytes() if trig_path else b""
    ik=ik_path.read_bytes() if ik_path else b""
    branches=common_branches_path.read_bytes() if common_branches_path else b""
    reactions=b""
    if reactions_path:
        original=reactions_path.read_bytes()
        if len(original)!=633*42:raise ValueError("original reaction table requires 633 records")
        indices=set()
        for slot in (295,355,357):
            index=struct.unpack_from('<I',bank,section[2]+4*slot)[0]
            if index>=struct.unpack_from('<H',bank,2)[0]:raise ValueError("supported Paul move missing")
            row=section[1]+56*index
            reaction,close=struct.unpack_from('<HH',bank,row+50)
            if reaction>=633 or close:raise ValueError("supported move requires unavailable reaction kind")
            indices.add(reaction)
        reactions=b''.join(struct.pack('<H',i)+original[i*42:(i+1)*42] for i in sorted(indices))
    body=body_path.read_bytes() if body_path else b""
    rsqrt=rsqrt_path.read_bytes() if rsqrt_path else b""
    if rsqrt_path is not None and len(rsqrt)!=768:raise ValueError("blend coefficients require a 768-byte reciprocal-sqrt window")
    if body_path is not None and len(body)!=432:raise ValueError("body tables require 384 sqrt, 32 joint and 16 radius bytes")
    ground=ground_path.read_bytes() if ground_path else b""
    if ground_path is not None and len(ground)!=1025+0x58C:
        raise ValueError("ground table blob requires 1025 atan bytes and 0x58C push bytes")
    if branches and (len(branches)%12 or len(branches)>65535*12):
        raise ValueError("EXE common branch table requires bounded 12-byte rows")
    if ik and len(ik)!=0x1F2:
        raise ValueError("PS1 IK normalized table image must be exactly 0x1F2 bytes")
    if ik and not trig:
        raise ValueError("IK requires the original sine/cosine lookup window")
    if trig and len(trig)!=0x2800:
        raise ValueError("PS1 Q12 sine table window must be exactly 0x2800 bytes")
    if spline and len(spline)!=0x660:
        raise ValueError("PS1 spline lookup blob must be exactly 0x660 bytes")
    out.mkdir(parents=True,exist_ok=True)
    header=f"""#ifndef T3_LOCAL_DIVMOT_H
#define T3_LOCAL_DIVMOT_H
#include <stdint.h>
#include "input_history.h"
#define T3_LOCAL_SEQUENCE_COUNT {len(sequences)}u
#define T3_LOCAL_SPLINE_BYTES {len(spline)}u
#define T3_LOCAL_TRIG_BYTES {len(trig)}u
#define T3_LOCAL_IK_BYTES {len(ik)}u
#define T3_LOCAL_COMMON_BRANCH_BYTES {len(branches)}u
#define T3_LOCAL_COMBAT_REACTION_BYTES {len(reactions)}u
extern const uint8_t t3_local_combat_reactions[{max(1,len(reactions))}];
#define T3_LOCAL_BODY_BYTES {len(body)}u
#define T3_LOCAL_RSQRT_BYTES {len(rsqrt)}u
extern const uint8_t t3_local_rsqrt_blob[{max(1,len(rsqrt))}];
extern const uint8_t t3_local_body_blob[{max(1,len(body))}];
#define T3_LOCAL_GROUND_BYTES {len(ground)}u
extern const uint8_t t3_local_ground_blob[{max(1,len(ground))}];
extern const uint8_t t3_local_common_branches[{max(1,len(branches))}];
extern const uint8_t t3_local_ik_blob[{max(1,len(ik))}];
extern const uint8_t t3_local_trig_blob[{max(1,len(trig))}];
extern const uint8_t t3_local_spline_blob[{max(1,len(spline))}];
extern const t3_command_sequence_t t3_local_sequences[{max(1,len(sequences))}];
#define T3_LOCAL_DIVMOT_BANK_BYTES {len(bank)}u
#define T3_LOCAL_DIVMOT_COMMON_BYTES {len(common)}u
extern const uint8_t t3_local_bank[{len(bank)}];
extern const uint8_t t3_local_common[{max(1,len(common))}];
#endif
"""
    def emit(name,data):
        return ("const uint8_t "+name+"["+str(max(1,len(data)))+"] = {\n"+
                "".join("    "+",".join(str(b) for b in data[i:i+24])+",\n"
                        for i in range(0,len(data),24))+
                ("    0,\n" if not data else "")+"};\n")
    impl=('#include "t3_local_divmot.h"\n'
          '/* PRIVATE local PS1 move data. Never publish derived objects/ISO. */\n'
          +emit("t3_local_bank",bank)+emit("t3_local_common",common)
          +emit("t3_local_spline_blob",spline)
          +emit("t3_local_trig_blob",trig)
          +emit("t3_local_ik_blob",ik)
          +emit("t3_local_ground_blob",ground)
          +emit("t3_local_body_blob",body)
          +emit("t3_local_rsqrt_blob",rsqrt)
          +emit("t3_local_combat_reactions",reactions)
          +emit("t3_local_common_branches",branches))
    impl += "const t3_command_sequence_t t3_local_sequences["+str(max(1,len(sequences)))+"] = {\\n"
    if not sequences:
        impl += "    {0},\\n"
    for command,window,steps in sequences:
        impl += ("    {0x%04X, %d, {" % (command,window) +
                 ",".join(str(step) for step in steps) + ",0}},\\n")
    impl += "};\\n"
    impl=impl.replace("\\n", "\n")
    (out/"combat_reactions.bin").write_bytes(reactions)
    (out/"t3_local_divmot.h").write_text(header)
    (out/"t3_local_divmot.c").write_text(impl)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bank",type=Path,required=True)
    p.add_argument("--common",type=Path)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--sequences",type=Path,help="private 104-entry input sequence JSON")
    p.add_argument("--spline-tables",type=Path,
                   help="optional PRIVATE 0x660-byte normalized PS1 spline lookup region")
    p.add_argument("--trig-table",type=Path,
                   help="optional PRIVATE 0x2800-byte original PS1 sine/cosine window")
    p.add_argument("--ik-tables",type=Path,
                   help="optional PRIVATE normalized 0x1F2-byte PS1 IK coefficients")
    p.add_argument("--common-branches",type=Path,help="private original EXE 12-byte branch rows")
    p.add_argument("--ground-tables",type=Path,help="private concatenated atan/push tables")
    p.add_argument("--body-tables",type=Path,help="private sqrt/body-joint/Paul-radius tables")
    p.add_argument("--rsqrt-table",type=Path,help="private original 768-byte blend reciprocal-sqrt window")
    p.add_argument("--combat-reactions",type=Path,help="private original 633-row reaction table; embeds only supported rows")
    a=p.parse_args()
    try:generate(a.bank,a.common,a.output,a.sequences,a.spline_tables,a.trig_table,a.ik_tables,a.common_branches,a.ground_tables,a.body_tables,a.combat_reactions,a.rsqrt_table)
    except (OSError,ValueError) as e:p.exit(1,f"private DIVMOT embed failed: {e}\n")
if __name__=="__main__":main()
