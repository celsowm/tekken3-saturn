#!/usr/bin/env python3
"""Embed PRIVATE compact DIVMOT banks into a local Saturn diagnostic build.

Never use original Namco banks or this generated C in public CI/releases.
"""
import argparse
from pathlib import Path
import json
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

def generate(bank_path,common_path,out,sequences_path=None,spline_path=None,trig_path=None,ik_path=None):
    bank=bank_path.read_bytes()
    _,section=sections_of(bank)
    if bank[1]==99:raise ValueError("main bank must be character bank, not type 99")
    common=common_path.read_bytes() if common_path else b""
    if common:
        sections_of(common)
        if common[1]!=99:raise ValueError("common bank must be divmot99")
    if len(bank)+len(common)>450000:
        raise ValueError("logic banks exceed 450-kilobyte experimental budget")
    sequences=load_sequences(sequences_path)
    spline=spline_path.read_bytes() if spline_path else b""
    trig=trig_path.read_bytes() if trig_path else b""
    ik=ik_path.read_bytes() if ik_path else b""
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
          +emit("t3_local_ik_blob",ik))
    impl += "const t3_command_sequence_t t3_local_sequences["+str(max(1,len(sequences)))+"] = {\\n"
    if not sequences:
        impl += "    {0},\\n"
    for command,window,steps in sequences:
        impl += ("    {0x%04X, %d, {" % (command,window) +
                 ",".join(str(step) for step in steps) + ",0}},\\n")
    impl += "};\\n"
    impl=impl.replace("\\n", "\n")
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
    a=p.parse_args()
    try:generate(a.bank,a.common,a.output,a.sequences,a.spline_tables,a.trig_table,a.ik_tables)
    except (OSError,ValueError) as e:p.exit(1,f"private DIVMOT embed failed: {e}\n")
if __name__=="__main__":main()
