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

CLOSE_BYTES = 64*4  # g_closeReactions after the 633 records (extract_psx_pose_tables.py)
THROW_BYTES = (397+21)*2  # g_throwVictimSlots after the close table
ATTACK_BYTES = 0x400  # built-in attack descriptors, pointers as offsets, after the throw slots

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

def fnv1a(data,value=2166136261):
    for b in data:value=((value^b)*16777619)&0xFFFFFFFF
    return value

def cold_file(bank,common,second=b""):
    """MOTION.BIN: big-endian header, then each bank at a 16-byte boundary.

    Version 1 holds the bank and common; version 2 appends the opponent's
    character bank (its size and offset follow the digest)."""
    bank_at=64;common_at=(bank_at+len(bank)+15)&~15
    second_at=(common_at+len(common)+15)&~15 if second else 0
    digest=fnv1a(common,fnv1a(bank))
    if second:digest=fnv1a(second,digest)
    fields=[0x54334D4F,2 if second else 1,len(bank),len(common),bank_at,common_at,digest]
    if second:fields+=[len(second),second_at]
    header=struct.pack(f'>{len(fields)}I',*fields).ljust(bank_at,b'\0')
    image=(header+bank).ljust(common_at,b'\0')+common
    if second:image=image.ljust(second_at,b'\0')+second
    return image,bank_at,common_at,second_at,digest

def generate(bank_path,common_path,out,sequences_path=None,spline_path=None,trig_path=None,ik_path=None,common_branches_path=None,ground_path=None,body_path=None,reactions_path=None,rsqrt_path=None,cold=False,second_path=None,ai_tables_path=None):
    bank=bank_path.read_bytes()
    # The CPU opponent's EXE tables (scripts/extract_psx_ai_tables.py).
    ai_tables=ai_tables_path.read_bytes() if ai_tables_path else b""
    if ai_tables and ai_tables[:4]!=b"T3AI":
        raise ValueError("CPU opponent tables must come from extract_psx_ai_tables.py")
    _,section=sections_of(bank)
    if bank[1]==99:raise ValueError("main bank must be character bank, not type 99")
    # The opponent's character bank, for host evaluators only: written to its
    # own t3_local_second.c so the Saturn image does not compile it.
    second=second_path.read_bytes() if second_path else b""
    if second:
        sections_of(second)
        if second[1]==99:raise ValueError("second bank must be a character bank, not type 99")
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
    reactions=close=throws=attacks=b""
    if reactions_path:
        original=reactions_path.read_bytes()
        if len(original)!=633*42+CLOSE_BYTES+THROW_BYTES+ATTACK_BYTES:
            raise ValueError("original reaction table requires 633 records, the 64-entry close table, "
                             "the throw victim slots and the built-in attack descriptors")
        close=original[633*42:633*42+CLOSE_BYTES]
        throws=original[633*42+CLOSE_BYTES:633*42+CLOSE_BYTES+THROW_BYTES]
        attacks=original[633*42+CLOSE_BYTES+THROW_BYTES:]
        # Every record a move row of either bank can name (+50 below 633);
        # higher words are throw slots, not records.
        indices=set()
        for data in (bank,*(d for d in (common,second) if d)):
            rows_at=sections_of(data)[1][1]
            for index in range(struct.unpack_from('<H',data,2)[0]):
                reaction,close_index=struct.unpack_from('<HH',data,rows_at+56*index+50)
                if reaction<633:indices.add(reaction)
                if close_index:
                    if close_index>=CLOSE_BYTES//4:raise ValueError("close reaction index outside the close table")
                    reaction=struct.unpack_from('<H',close,4*close_index)[0]
                    if reaction<633:indices.add(reaction)
        for slot in (295,355,357):
            index=struct.unpack_from('<I',bank,section[2]+4*slot)[0]
            if index>=struct.unpack_from('<H',bank,2)[0]:raise ValueError("supported Paul move missing")
            reaction=struct.unpack_from('<H',bank,section[1]+56*index+50)[0]
            if reaction>=633:raise ValueError("supported move requires unavailable reaction kind")
        reactions=b''.join(struct.pack('<H',i)+original[i*42:(i+1)*42] for i in sorted(indices))
    body=body_path.read_bytes() if body_path else b""
    rsqrt=rsqrt_path.read_bytes() if rsqrt_path else b""
    if rsqrt_path is not None and len(rsqrt)!=768:raise ValueError("blend coefficients require a 768-byte reciprocal-sqrt window")
    if body_path is not None and len(body)!=432:raise ValueError("body tables require 384 sqrt, 32 joint and 16 radius bytes")
    ground=ground_path.read_bytes() if ground_path else b""
    if ground_path is not None and len(ground)!=1025+0x58C+16+24:
        raise ValueError("ground table blob requires 1025 atan bytes, 0x58C push bytes, "
                         "the 16-byte launch push table and the 24-byte air kinds")
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
#define T3_LOCAL_AI_TABLE_BYTES {len(ai_tables)}u
extern const uint8_t t3_local_ai_tables[{max(1,len(ai_tables))}];
#define T3_LOCAL_COMBAT_REACTION_BYTES {len(reactions)}u
extern const uint8_t t3_local_combat_reactions[{max(1,len(reactions))}];
#define T3_LOCAL_COMBAT_CLOSE_BYTES {len(close)}u
extern const uint8_t t3_local_combat_close[{max(1,len(close))}];
#define T3_LOCAL_COMBAT_THROW_BYTES {len(throws)}u
extern const uint8_t t3_local_combat_throws[{max(1,len(throws))}];
#define T3_LOCAL_COMBAT_ATTACK_BYTES {len(attacks)}u
extern const uint8_t t3_local_combat_attacks[{max(1,len(attacks))}];
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
#define T3_LOCAL_SECOND_BANK_BYTES {len(second)}u
"""
    if cold:
        image,bank_at,common_at,second_at,digest=cold_file(bank,common,second)
        header+=f"""/* Filled from MOTION.BIN by t3_cold_data_load before first use. */
#define T3_LOCAL_COLD_FILE_BYTES {len(image)}u
#define T3_LOCAL_COLD_BANK_OFFSET {bank_at}u
#define T3_LOCAL_COLD_COMMON_OFFSET {common_at}u
#define T3_LOCAL_COLD_FNV1A 0x{digest:08X}u
extern uint8_t t3_local_bank[{len(bank)}];
extern uint8_t t3_local_common[{max(1,len(common))}];
"""
        if second:
            header+=f"""/* The opponent's bank, also from MOTION.BIN (version 2). */
#define T3_LOCAL_COLD_SECOND_OFFSET {second_at}u
extern uint8_t t3_local_second_bank[{len(second)}];
"""
        else:
            header+=f"""extern const uint8_t t3_local_second_bank[1];
"""
        header+="#endif\n"
    else:
        header+=f"""extern const uint8_t t3_local_bank[{len(bank)}];
extern const uint8_t t3_local_common[{max(1,len(common))}];
/* In t3_local_second.c; 0 bytes without --second-bank. */
extern const uint8_t t3_local_second_bank[{max(1,len(second))}];
#endif
"""
    def cold_storage(bank_bytes,common_bytes,second_bytes):
        return ("#if defined(__sh__)\n"
                "#define T3_COLD_STORAGE __attribute__((section(\".wram_l\"),aligned(16)))\n"
                "#else\n#define T3_COLD_STORAGE\n#endif\n"
                f"uint8_t t3_local_bank[{bank_bytes}] T3_COLD_STORAGE;\n"
                f"uint8_t t3_local_common[{max(1,common_bytes)}] T3_COLD_STORAGE;\n"
                +(f"uint8_t t3_local_second_bank[{second_bytes}] T3_COLD_STORAGE;\n" if second_bytes else ""))
    def emit(name,data):
        return ("const uint8_t "+name+"["+str(max(1,len(data)))+"] = {\n"+
                "".join("    "+",".join(str(b) for b in data[i:i+24])+",\n"
                        for i in range(0,len(data),24))+
                ("    0,\n" if not data else "")+"};\n")
    impl=('#include "t3_local_divmot.h"\n'
          '/* PRIVATE local PS1 move data. Never publish derived objects/ISO. */\n'
          +(cold_storage(len(bank),len(common),len(second)) if cold else
            emit("t3_local_bank",bank)+emit("t3_local_common",common))
          +emit("t3_local_spline_blob",spline)
          +emit("t3_local_trig_blob",trig)
          +emit("t3_local_ik_blob",ik)
          +emit("t3_local_ground_blob",ground)
          +emit("t3_local_body_blob",body)
          +emit("t3_local_rsqrt_blob",rsqrt)
          +emit("t3_local_combat_reactions",reactions)
          +emit("t3_local_combat_close",close)
          +emit("t3_local_combat_throws",throws)
          +emit("t3_local_combat_attacks",attacks)
          +emit("t3_local_common_branches",branches)
          +emit("t3_local_ai_tables",ai_tables))
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
    # A cold build loads the second bank from MOTION.BIN instead.
    (out/"t3_local_second.c").write_text('#include "t3_local_divmot.h"\n'
        '/* PRIVATE local PS1 move data. Never publish derived objects/ISO. */\n'
        +("" if cold and second else emit("t3_local_second_bank",second)))
    if cold:(out/"MOTION.BIN").write_bytes(image)

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
    p.add_argument("--second-bank",type=Path,help="private opponent character bank for host evaluators")
    p.add_argument("--ai-tables",type=Path,help="private CPU opponent tables (extract_psx_ai_tables.py)")
    p.add_argument("--cold-file",action="store_true",
                   help="write the two DIVMOT banks to MOTION.BIN and emit Low WRAM storage instead of embedding them")
    a=p.parse_args()
    try:generate(a.bank,a.common,a.output,a.sequences,a.spline_tables,a.trig_table,a.ik_tables,a.common_branches,a.ground_tables,a.body_tables,a.combat_reactions,a.rsqrt_table,a.cold_file,a.second_bank,a.ai_tables)
    except (OSError,ValueError) as e:p.exit(1,f"private DIVMOT embed failed: {e}\n")
if __name__=="__main__":main()
