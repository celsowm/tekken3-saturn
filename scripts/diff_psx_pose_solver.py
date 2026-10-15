#!/usr/bin/env python3
"""Differentially test the C/Saturn PS1 pose solver against pinned Tekken3Remake.

The REFERENCE is imported from the checkout's existing noncommercial
submodule at third_party/Tekken3Remake/tools/research/pose.py. This file
does not copy/redistribute it. Inputs use normalized PRIVATE lookup
data produced by extract_psx_ik_tables.py plus 49-channel pose samples.
The program never supplies copyrighted assets or claims PS1 MIPS parity.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

POSE=struct.Struct("<49h")
MATRIX_COUNT=18
RESULT=struct.Struct("<I162h")
SLOTS=(0,1,11,2,3,7,6,10,14,17)
ONE=4096
MASK=(1<<18)-1

class DifferentialError(ValueError):
    pass

class NormalizedTables:
    """Adapter for upstream Python oracle API; no guessed USA memory address."""
    def __init__(self,trig:bytes,ik:bytes,reference):
        if len(trig)!=0x2800 or len(ik)!=0x1F2:
            raise DifferentialError("expected exact private 0x2800 trig and 0x1F2 IK blobs")
        self.trig=trig
        self.ik=ik
        self.ref=reference
    def sin(self,angle):
        return struct.unpack_from("<h",self.trig,((angle&0xffff)>>3)&0x1ffe)[0]
    def cos(self,angle):
        return struct.unpack_from("<h",self.trig,0x800+(((angle&0xffff)>>3)&0x1ffe))[0]
    def u8(self,addr):
        i=addr-self.ref.EULER_SLOTS
        if not 0<=i<len(SLOTS):
            raise DifferentialError(f"unexpected reference Euler slot address {addr:x}")
        return SLOTS[i]
    def s16(self,addr):
        if self.ref.SQRT_TABLE<=addr<self.ref.SQRT_TABLE+0x180:
            base=addr-self.ref.SQRT_TABLE
        elif self.ref.LIMB_BASIS<=addr<self.ref.LIMB_BASIS+18:
            base=0x180+addr-self.ref.LIMB_BASIS
        else:
            raise DifferentialError(f"unexpected reference table access {addr:x}")
        if base&1:raise DifferentialError("unaligned reference table read")
        return struct.unpack_from("<h",self.ik,base)[0]
    def vec3(self,addr):
        if self.ref.SKELETON_OFFSETS<=addr<self.ref.SKELETON_OFFSETS+48:
            at=0x192+addr-self.ref.SKELETON_OFFSETS
        elif self.ref.LIMB_OFFSETS<=addr<self.ref.LIMB_OFFSETS+48:
            at=0x1c2+addr-self.ref.LIMB_OFFSETS
        else:
            raise DifferentialError(f"unexpected reference vector access {addr:x}")
        if at%4:raise DifferentialError("unaligned reference vector")
        return list(struct.unpack_from("<3i",self.ik,at))

def load_reference(source:Path):
    if not source.is_file():
        raise DifferentialError("pinned upstream reference missing: initialize "
                                "third_party/Tekken3Remake submodule locally")
    spec=importlib.util.spec_from_file_location("t3_ps1_pose_upstream",source)
    if not spec or not spec.loader:
        raise DifferentialError(f"unable to load research pose module: {source}")
    module=importlib.util.module_from_spec(spec)
    # dataclasses requires module to be present during import.
    sys.modules[spec.name]=module
    spec.loader.exec_module(module)
    return module

def compare(runner:Path,upstream:Path,trig:bytes,ik:bytes,poses:bytes):
    if not poses or len(poses)%POSE.size:
        raise DifferentialError("input must contain complete 49-channel pose frames")
    if len(poses)>POSE.size*100000:
        raise DifferentialError("pose stream is too large (limit 100000 frames)")
    reference=load_reference(upstream)
    tables=NormalizedTables(trig,ik,reference)
    with tempfile.TemporaryDirectory(prefix="t3-pose-diff-") as d:
        d=Path(d)
        for name,data in (("trig.bin",trig),("ik.bin",ik),("pose.bin",poses)):
            (d/name).write_bytes(data)
        process=subprocess.run([str(runner.resolve()),str(d/"trig.bin"),
            str(d/"ik.bin"),str(d/"pose.bin"),str(d/"result.bin")],
            capture_output=True,text=True,check=False)
        if process.returncode:
            raise DifferentialError(f"C runner failed ({process.returncode}): "
                                    f"{process.stderr[:1200]}")
        got=(d/"result.bin").read_bytes()
    if len(got)!=len(poses)//POSE.size*RESULT.size:
        raise DifferentialError(f"C runner output size mismatch: {len(got)}")
    mismatches=[]
    compared=0
    frames=0
    incomplete=0
    for frame,at in enumerate(range(0,len(poses),POSE.size)):
        vals=POSE.unpack_from(poses,at)
        reference_slots=[[0]*9 for _ in range(MATRIX_COUNT)]
        try:
            reference.pose_build_matrices(list(vals),reference_slots,tables)
        except (ValueError,ArithmeticError,IndexError) as e:
            raise DifferentialError(f"reference solver failed at frame {frame}: {e}") from e
        c_values=RESULT.unpack_from(got,frame*RESULT.size)
        mask=c_values[0]
        if mask&~MASK:
            raise DifferentialError(f"invalid C solver mask 0x{mask:X}, frame {frame}")
        if mask!=MASK:incomplete+=1
        frames+=1
        for slot in range(MATRIX_COUNT):
            if not(mask&(1<<slot)):continue
            for index in range(9):
                compared+=1
                observed=c_values[1+slot*9+index]
                expected=reference_slots[slot][index]
                if observed!=expected:
                    mismatches.append({
                        "frame":frame,"slot":slot,"component":index,
                        "expected":expected,"actual":observed,
                        "difference":observed-expected,
                    })
    return {"frames":frames,"components_compared":compared,
            "incomplete_frames":incomplete,
            "mismatch_count":len(mismatches),
            "first_differences":mismatches[:30]}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runner",type=Path,required=True,
                   help="compiled tools/psx_pose_eval.c executable")
    p.add_argument("--trig",type=Path,required=True)
    p.add_argument("--ik",type=Path,required=True)
    p.add_argument("--poses",type=Path,required=True,
                   help="raw concatenated little-endian signed int16[49] pose vectors")
    p.add_argument("--upstream",type=Path,
                   default=Path("third_party/Tekken3Remake/tools/research/pose.py"))
    p.add_argument("--strict-complete",action="store_true",
                   help="fail if any sampled frame has fewer than 18 available slots")
    a=p.parse_args()
    try:
        report=compare(a.runner,a.upstream,a.trig.read_bytes(),
                       a.ik.read_bytes(),a.poses.read_bytes())
    except (OSError,DifferentialError) as exc:
        p.exit(2,f"Pose differential failed: {exc}\n")
    print(json.dumps(report,indent=2))
    return 1 if report["mismatch_count"] or (a.strict_complete and
                                            report["incomplete_frames"]) else 0

if __name__=="__main__":
    raise SystemExit(main())
