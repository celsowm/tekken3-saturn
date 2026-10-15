#!/usr/bin/env python3
"""Recover the 104 original Tekken 3 command sequences from a PRIVATE PS-X EXE.

The US and Japan revisions have different ROM offsets; therefore this
importer searches for 104 adjacent, structurally validated EXE pointers
instead of treating a Japanese virtual address as an American one.
No game data is downloaded or emitted into the source repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXPECTED_US_SHA = "fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c"
TABLE_LENGTHS = ((0xC00E,63),(0xC7FF,41))
N = sum(count for _,count in TABLE_LENGTHS)
MAX_STEPS = 32
MAX_WINDOW = 60
EXE_HEADER = 0x800

class SequenceError(ValueError): pass

def u16(data,offset):
    if offset<0 or offset+2>len(data):raise SequenceError("u16 out of executable bounds")
    return struct.unpack_from("<H",data,offset)[0]

def u32(data,offset):
    if offset<0 or offset+4>len(data):raise SequenceError("u32 out of executable bounds")
    return struct.unpack_from("<I",data,offset)[0]

def load_base(data):
    if data[:8]!=b"PS-X EXE" or len(data)<EXE_HEADER:
        raise SequenceError("expected original PS-X EXE with 0x800-byte header")
    base=u32(data,0x18)
    if base<0x80010000 or base>=0x80200000:
        raise SequenceError("implausible PS1 executable load base")
    return base

def va_offset(va,load_address,exe_len):
    off=va-load_address+EXE_HEADER
    if va%2 or off<EXE_HEADER or off+2>exe_len:
        raise SequenceError("sequence pointer outside executable body")
    return off

def read_sequence(data,va,base):
    off=va_offset(va,base,len(data))
    window=struct.unpack_from("<h",data,off)[0]
    if not 1<=window<=MAX_WINDOW:
        raise SequenceError("unsupported original sequence window")
    steps=[]
    for i in range(MAX_STEPS+1):
        step=u16(data,off+2+2*i)
        if step==0:
            if not steps:raise SequenceError("empty command sequence")
            return (window,steps)
        if i==MAX_STEPS:raise SequenceError("sequence exceeds supported maximum")
        if step&0x00F0 or (step&15)>9 or step>>8>0x1F:
            raise SequenceError("invalid direction or PS1 button specification")
        steps.append(step)
    raise SequenceError("unterminated input sequence")

def at_table(data,base,offset):
    if offset<EXE_HEADER or offset+N*4>len(data) or offset%4:
        raise SequenceError("table offset is not aligned/in file")
    output=[]
    idx=0
    for first,n in TABLE_LENGTHS:
        for i in range(n):
            va=u32(data,offset+(idx+i)*4)
            window,steps=read_sequence(data,va,base)
            output.append({"command":first+i,"window":window,"steps":steps})
        idx+=n
    return output

def detect_table(data):
    base=load_base(data)
    candidates=[]
    # Early pointer validation rejects nearly all ordinary exe words before
    # trying the expensive 104-entry candidate.
    for offset in range(EXE_HEADER,len(data)-N*4+1,4):
        try:
            first=u32(data,offset)
            last=u32(data,offset+(N-1)*4)
            read_sequence(data,first,base)
            read_sequence(data,last,base)
            rows=at_table(data,base,offset)
        except SequenceError:
            continue
        candidates.append((offset,rows))
        if len(candidates)>1:break
    if not candidates:raise SequenceError("could not validate the 104-pointer sequence table")
    if len(candidates)>1:
        raise SequenceError("ambiguous table signatures: pass --table-offset explicitly")
    return candidates[0]

def extract(data,table_offset=None,expected_sha=EXPECTED_US_SHA):
    sha=hashlib.sha256(data).hexdigest()
    if expected_sha and sha!=expected_sha:
        raise SequenceError("unsupported US executable SHA256="+sha)
    base=load_base(data)
    offset,rows=(detect_table(data) if table_offset is None else
                 (table_offset,at_table(data,base,table_offset)))
    return {"schema":1,"source":"Tekken 3 PS-X EXE (private)","exe_sha256":sha,
            "table_file_offset":offset,"load_base":f"0x{base:08X}",
            "count":len(rows),"sequences":rows,
            "copyright":"User-provided PS1 data; do not distribute generated tables"}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe",type=Path,required=True,help="locally extracted SLUS_004.02 executable")
    p.add_argument("--output",type=Path,default=Path("assets/imported/tekken3-usa/moves/input_sequences.json"))
    p.add_argument("--table-offset",type=lambda value:int(value,0),
                   help="explicit EXE file offset, if automatic discovery is ambiguous")
    a=p.parse_args()
    try:
        report=extract(a.exe.read_bytes(),a.table_offset)
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf8")
    except (OSError,SequenceError,struct.error) as error:
        p.exit(1,f"PS1 input sequence import failed: {error}\n")
    print(f"Recovered {report['count']} sequences: {a.output}")

if __name__=="__main__":main()
