#!/usr/bin/env python3
"""Compare PRIVATE Saturn PS1 KMD pose snapshots against original PS1 oracle JSONL.

The Saturn binary consists of concatenated 564-byte records captured from
the debugger symbol g_t3_pose_trace[0] or [1] after each simulation tick.
The original PS1 reference must be obtained independently with a local
emulator/MIPS trace harness; this tool does not invent reference values.
No game-derived data is committed or downloaded.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

MAGIC=b"T3PX"
VERSION=1
JOINTS=18
HEADER=struct.Struct("<4sHHIHHHHI")
JOINT=struct.Struct("<3i9h")
RECORD_BYTES=HEADER.size+JOINTS*JOINT.size
BODY_MASK=(1<<JOINTS)-1

class TraceError(ValueError):
    pass

def records_from_bytes(data):
    if len(data)%RECORD_BYTES:
        raise TraceError(f"unaligned binary snapshots: {len(data)} is not a multiple of {RECORD_BYTES}")
    if not data:
        raise TraceError("empty Saturn trace")
    rows=[]
    for offset in range(0,len(data),RECORD_BYTES):
        tag,version,count,tick,slot,frame,fighter,reserved,mask=HEADER.unpack_from(data,offset)
        if tag!=MAGIC or version!=VERSION or count!=JOINTS or reserved or fighter>1 or mask&~BODY_MASK:
            raise TraceError(f"invalid record header at offset 0x{offset:X}")
        parts=[]
        for j in range(JOINTS):
            start=offset+HEADER.size+j*JOINT.size
            values=JOINT.unpack_from(data,start)
            if mask&(1<<j):
                parts.append({"origin":list(values[:3]),"rotation":list(values[3:])})
            else:
                if any(values):
                    raise TraceError(f"invalid joint {j} has nonzero payload at 0x{start:X}")
                parts.append(None)
        rows.append({"tick":tick,"fighter":fighter,"slot":slot,
                     "frame":frame,"mask":mask,"parts":parts})
    return rows

def validate_jsonl(data):
    rows=[]
    for lineno,line in enumerate(data.splitlines(),1):
        if not line.strip():continue
        try:
            row=json.loads(line)
            key=tuple(row[key] for key in ("tick","fighter","slot","frame","mask"))
            parts=row["parts"]
        except (ValueError,KeyError,TypeError) as exc:
            raise TraceError(f"invalid JSONL line {lineno}: {exc}") from exc
        if (not all(type(v) is int for v in key) or key[0]<0 or key[1] not in (0,1) or
            not 0<=key[2]<=65535 or not 0<=key[3]<=65535 or
            not 0<=key[4]<=BODY_MASK or not isinstance(parts,list) or len(parts)!=JOINTS):
            raise TraceError(f"invalid PS1 frame schema at line {lineno}")
        for j,part in enumerate(parts):
            if not (key[4]&(1<<j)):
                if part is not None:raise TraceError(f"invalid joint {j}: expected null on line {lineno}")
            elif (not isinstance(part,dict) or
                  any(not isinstance(part.get(k),list) or
                      len(part[k])!=n or
                      not all(type(x) is int for x in part[k])
                      for k,n in (("origin",3),("rotation",9)))):
                raise TraceError(f"invalid joint {j} at line {lineno}")
        rows.append(row)
    if not rows:raise TraceError("empty PS1 reference JSONL")
    return rows

def compare(saturn,reference,origin_tolerance=0,rotation_tolerance=0,
            allow_incomplete=False):
    if origin_tolerance<0 or rotation_tolerance<0:
        raise TraceError("tolerances must be nonnegative")
    def index(rows,label):
        result={}
        for row in rows:
            key=(row["tick"],row["fighter"])
            if key in result:raise TraceError(f"duplicate {label} frame {key}")
            result[key]=row
        return result
    got=index(saturn,"Saturn")
    expected=index(reference,"PS1")
    errors=[]
    missing=sorted(set(expected)-set(got))
    extra=sorted(set(got)-set(expected))
    for key in missing:errors.append(f"missing Saturn frame {key}")
    for key in extra:errors.append(f"extra Saturn frame {key}")
    joints_compared=0
    for key in sorted(set(got)&set(expected)):
        a,b=got[key],expected[key]
        if (a["slot"],a["frame"])!=(b["slot"],b["frame"]):
            errors.append(f"{key}: move mismatch Saturn={a['slot']}/{a['frame']} PS1={b['slot']}/{b['frame']}")
        if a["mask"]!=b["mask"]:
            errors.append(f"{key}: valid-joint masks differ Saturn=0x{a['mask']:X} PS1=0x{b['mask']:X}")
        if not allow_incomplete and (a["mask"]!=BODY_MASK or b["mask"]!=BODY_MASK):
            errors.append(f"{key}: incomplete original pose (need 18/18)")
        for i in range(JOINTS):
            if not ((a["mask"]&b["mask"]) & (1<<i)):continue
            joints_compared+=1
            for field,tol in (("origin",origin_tolerance),("rotation",rotation_tolerance)):
                for component,(u,v) in enumerate(zip(a["parts"][i][field],b["parts"][i][field])):
                    if abs(u-v)>tol:
                        errors.append(f"{key}: part {i} {field}[{component}] Saturn={u} PS1={v} diff={u-v}")
    if not joints_compared:errors.append("no jointly valid parts could be compared")
    return {"frames_compared":len(set(got)&set(expected)),
            "joints_compared":joints_compared,
            "mismatches":len(errors),
            "details":errors[:20],
            "truncated":max(0,len(errors)-20)}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--saturn",type=Path,required=True,
                   help="concatenated binary debugger snapshots (g_t3_pose_trace)")
    p.add_argument("--oracle",type=Path,
                   help="independent, PRIVATE original PS1 reference JSONL")
    p.add_argument("--dump-jsonl",action="store_true",
                   help="decode binary snapshots as JSONL on stdout instead of comparison")
    p.add_argument("--origin-tolerance",type=int,default=0)
    p.add_argument("--rotation-tolerance",type=int,default=0)
    p.add_argument("--allow-incomplete",action="store_true")
    a=p.parse_args()
    if not a.dump_jsonl and not a.oracle:p.error("--oracle is required unless --dump-jsonl")
    try:
        sat=records_from_bytes(a.saturn.read_bytes())
        if a.dump_jsonl:
            for row in sat:print(json.dumps(row,separators=(",",":")))
            return 0
        expected=validate_jsonl(a.oracle.read_text(encoding="utf8"))
        report=compare(sat,expected,a.origin_tolerance,
                       a.rotation_tolerance,a.allow_incomplete)
        print(json.dumps(report,indent=2))
        return 1 if report["mismatches"] else 0
    except (OSError,UnicodeError,TraceError) as exc:
        print(f"Pose comparison failed: {exc}",file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(main())
