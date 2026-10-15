#!/usr/bin/env python3
"""Extract PRIVATE PS1 FK/IK lookup data from the user's Tekken 3 USA EXE.

Table locations MUST be independently verified in the USA executable.
Never reuse hardcoded Japanese-revision virtual addresses or publish output.
Output shape is consumed by psx_ik.c and the local Saturn C asset embedder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from extract_psx_sequences import EXPECTED_US_SHA, load_base, SequenceError

SIZES={"sine":0x2800,"sqrt":0x180,"basis":0x12,"skeleton":0x30,"limb":0x30}
IK_LEN=sum(SIZES[name] for name in ("sqrt","basis","skeleton","limb"))

class TableError(ValueError): pass

def normalize(exe, offsets, expected_sha=EXPECTED_US_SHA):
    if not isinstance(exe,(bytes,bytearray)):
        raise TableError("expected PS-X EXE bytes")
    digest=hashlib.sha256(exe).hexdigest()
    if expected_sha is not None and digest != expected_sha:
        raise TableError("unsupported US PS-X EXE SHA256="+digest)
    try:
        base=load_base(exe)
    except SequenceError as exc:
        raise TableError(str(exc)) from exc
    if set(offsets)!=set(SIZES):
        raise TableError("all five table file offsets are required")
    ranges=[]
    chunks={}
    for name,size in SIZES.items():
        at=offsets[name]
        if not isinstance(at,int) or at<0x800 or (at&1) or at+size>len(exe):
            raise TableError(f"{name} table offset is unaligned/out of PS-X EXE body")
        ranges.append((at,at+size,name))
        chunks[name]=bytes(exe[at:at+size])
    ranges.sort()
    for a,b in zip(ranges,ranges[1:]):
        if a[1]>b[0]:
            raise TableError(f"overlapping source regions: {a[2]} and {b[2]}")

    sine=chunks["sine"]
    sq=chunks["sqrt"]
    basis=chunks["basis"]
    # Fail clearly on obviously invalid offsets. Test fixtures may carry
    # synthetic coefficients but still have the same numerical contracts.
    sin0=struct.unpack_from("<h",sine,0)[0]
    cos0=struct.unpack_from("<h",sine,0x800)[0]
    if abs(sin0)>64 or not 3900<=cos0<=4200:
        raise TableError("sine/cosine origin does not look like Q12 0/4096")
    mant=struct.unpack("<192h",sq)
    if any(n<=0 or n>16384 for n in mant):
        raise TableError("invalid PS1 SquareRoot0 mantissa lookup")
    mat=struct.unpack("<9h",basis)
    if any(abs(n)>8192 for n in mat):
        raise TableError("invalid Q12 limb basis")
    vectors=struct.unpack("<24i",chunks["skeleton"]+chunks["limb"])
    if any(abs(n)>1_000_000 for n in vectors):
        raise TableError("invalid PS1 skeletal/IK offsets")

    ik=b"".join(chunks[name] for name in ("sqrt","basis","skeleton","limb"))
    assert len(sine)==0x2800 and len(ik)==IK_LEN==0x1F2
    return sine,ik,{
        "schema":1,
        "source":"PS-X EXE, region-specific offsets supplied by user",
        "source_sha256":digest,
        "load_base":f"0x{base:08X}",
        "file_offsets":{name:f"0x{offsets[name]:X}" for name in SIZES},
        "trig_sha256":hashlib.sha256(sine).hexdigest(),
        "ik_sha256":hashlib.sha256(ik).hexdigest(),
        "trig_bytes":len(sine),
        "ik_bytes":len(ik),
        "visibility":"PRIVATE – contains copyrighted PS1 authored lookup data",
    }

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe",required=True,type=Path)
    p.add_argument("--output",type=Path,
                   default=Path("assets/imported/tekken3-usa/ik"))
    for name in SIZES:
        p.add_argument(f"--{name}-offset",type=lambda v:int(v,0),required=True,
                       help="verified absolute file offset in the USA PS-X EXE")
    args=p.parse_args()
    offsets={name:getattr(args,name+"_offset") for name in SIZES}
    try:
        sine,ik,meta=normalize(args.exe.read_bytes(),offsets)
        args.output.mkdir(parents=True,exist_ok=True)
        (args.output/"psx_trig.bin").write_bytes(sine)
        (args.output/"psx_ik.bin").write_bytes(ik)
        (args.output/"psx_ik_manifest.json").write_text(
            json.dumps(meta,indent=2,sort_keys=True)+"\n",encoding="utf8")
    except (TableError,OSError) as error:
        p.exit(1,f"PS1 private IK table extraction failed: {error}\n")
    print(f"Private PS1 tables: {args.output} (sha256={meta['source_sha256']})")

if __name__=="__main__":
    main()
