#!/usr/bin/env python3
"""Batch original DIVMOT pose sampling and C-vs-upstream FK/IK comparison.

This is an OFFLINE *private* validation tool. The only source inputs are
caller-supplied DIVMOT banks, coefficients, and two compiled C host binaries.
No ROM download, data redistribution or invented PS1 reference captures.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from divmot_to_logic import FormatError, sections_of
from diff_psx_pose_solver import DifferentialError, compare

SLOTS=4023
MOVE_SIZE=56
EMPTY=0x3FFFFFFF
COMMON_FLAG=0x40000000
MAX_BYTES=4*1024*1024
MAX_CAP=128

class BatchError(ValueError):
    pass

def read_bank(path, *, common=False):
    raw=Path(path).read_bytes()
    if not 64<=len(raw)<=MAX_BYTES:
        raise BatchError("bank size is outside the supported DIVMOT range")
    try:
        rows,sections=sections_of(raw)
    except FormatError as exc:
        raise BatchError(f"invalid DIVMOT section directory: {exc}") from exc
    if (raw[1]==99) != common:
        raise BatchError("character/common bank types were swapped")
    return {"raw":raw,"rows":rows,"sections":sections,"kind":raw[1],
            "sha256":hashlib.sha256(raw).hexdigest()}

def inspect_slot(character,shared,index):
    """Discover one slot without interpreting a motion as arbitrary bytes."""
    if not 0<=index<SLOTS:
        return None,"slot_out_of_range"
    raw=character["raw"]
    ref=struct.unpack_from("<I",raw,character["sections"][2]+index*4)[0]
    if ref==EMPTY:
        return None,"empty"
    owner=character
    if ref>=COMMON_FLAG:
        if ref>=0x80000000:
            return None,"unsupported_reference"
        if shared is None:return None,"missing_common_bank"
        owner=shared
        ref-=COMMON_FLAG
    if ref>=owner["rows"]:
        return None,"invalid_move_row"
    row_offset=owner["sections"][1]+ref*MOVE_SIZE
    anim=struct.unpack_from("<I",owner["raw"],row_offset)[0]
    bank=owner
    if anim>=COMMON_FLAG:
        if anim>=0x80000000:
            return None,"unsupported_animation_pointer"
        if shared is None:return None,"missing_common_bank"
        bank=shared
        anim-=COMMON_FLAG
    section=bank["sections"]
    length=section[7]-section[6]
    if not length:return None,"no_motion_section"
    if anim<2 or anim+50>length:
        return None,"invalid_animation_header"
    offset=section[6]+anim
    frames=bank["raw"][offset]
    if not frames:return None,"zero_frames"
    return {"slot":index,"move_row":ref,"owner":owner["kind"],
            "animation_bank":bank["kind"],"animation_offset":anim,
            "frames":frames},None

def slot_indices(text):
    if text is None:return list(range(SLOTS))
    result=set()
    for part in text.split(","):
        part=part.strip()
        if not part:
            raise BatchError("empty entry in --slots")
        bounds=part.split("-")
        if len(bounds)>2 or any(not b.isdecimal() for b in bounds):
            raise BatchError("slots must be comma-separated integers or inclusive ranges")
        lo=int(bounds[0])
        hi=int(bounds[-1])
        if lo>hi or hi>=SLOTS:
            raise BatchError(f"invalid slot range: {part}")
        result.update(range(lo,hi+1))
    if not result:raise BatchError("no selected slots")
    return sorted(result)

def discover(character,shared,indices):
    reasons=Counter()
    animations={}
    for slot in indices:
        info,reason=inspect_slot(character,shared,slot)
        if reason:
            reasons[reason]+=1
            continue
        key=(info["animation_bank"],info["animation_offset"])
        if key in animations:
            animations[key]["duplicate_slots"]+=1
        else:
            info["duplicate_slots"]=0
            animations[key]=info
    return list(animations.values()),reasons

def spread(candidates,limit):
    """Pick across the full slot range instead of biasing toward low IDs."""
    if len(candidates)<=limit:return candidates
    if limit==1:return [candidates[0]]
    picks=[(i*(len(candidates)-1))//(limit-1) for i in range(limit)]
    return [candidates[i] for i in picks]

def execute(batch,*,character_path,shared_path,spline_path,trig,ik,exporter,
            solver,upstream,max_frames,strict_complete=False,timeout=45):
    results=[]
    reasons=Counter()
    compared=0
    incomplete=0
    with tempfile.TemporaryDirectory(prefix="t3-private-batch-") as directory:
        folder=Path(directory)
        for item in batch:
            sample=folder/"sample.bin"
            frames=min(max_frames,item["frames"])
            cmd=[str(exporter.resolve()),str(character_path.resolve()),
                 str(shared_path.resolve()) if shared_path else "-",
                 str(spline_path.resolve()) if spline_path else "-",
                 str(item["slot"]),"0",str(frames),str(sample)]
            record={**item,"tested_frames":frames}
            try:
                run=subprocess.run(cmd,capture_output=True,text=True,
                                   check=False,timeout=timeout)
                if run.returncode:
                    reason="export_failed"
                    if "spline table missing" in run.stderr:reason="missing_spline"
                    record.update({"status":"skipped","reason":reason,
                                   "diagnostic":run.stderr.strip()[:240]})
                    reasons[reason]+=1
                    results.append(record)
                    continue
                if not sample.is_file() or sample.stat().st_size!=98*frames:
                    record.update({"status":"skipped","reason":"wrong_export_size"})
                    reasons["wrong_export_size"]+=1
                    results.append(record)
                    continue
                report=compare(solver,upstream,trig,ik,sample.read_bytes())
            except subprocess.TimeoutExpired:
                record.update({"status":"skipped","reason":"export_timeout"})
                reasons["export_timeout"]+=1
                results.append(record)
                continue
            except (DifferentialError,OSError) as exc:
                record.update({"status":"skipped","reason":"comparison_failed",
                               "diagnostic":str(exc)[:240]})
                reasons["comparison_failed"]+=1
                results.append(record)
                continue
            record.update({"status":"mismatch" if report["mismatch_count"] else
                           ("incomplete" if report["incomplete_frames"] else "match"),
                           **report})
            compared+=report["components_compared"]
            incomplete+=report["incomplete_frames"]
            results.append(record)
    matched=sum(r["status"]=="match" for r in results)
    diff=sum(r["status"]=="mismatch" for r in results)
    return {"selected_animations":len(batch),"tested_animations":
            sum(r["status"] not in ("skipped",) for r in results),
            "matched_animations":matched,"mismatched_animations":diff,
            "incomplete_frames":incomplete,"components_compared":compared,
            "skipped_during_execution":dict(sorted(reasons.items())),
            "results":results,
            "pass":bool(compared and not diff and
                        (not strict_complete or incomplete==0))}

def batch(character_path,shared_path,spline_path,trig_path,ik_path,
          exporter,solver,upstream,slots=None,max_slots=24,
          max_frames=32,strict_complete=False,fail_on_skip=False):
    if not 1<=max_slots<=MAX_CAP:raise BatchError(f"--max-slots must be 1..{MAX_CAP}")
    if not 1<=max_frames<=255:raise BatchError("--max-frames must be 1..255")
    if not exporter.is_file() or not solver.is_file() or not upstream.is_file():
        raise BatchError("native exporter/solver or pinned reference source missing")
    c=read_bank(character_path)
    shared=read_bank(shared_path,common=True) if shared_path else None
    choices,discovery_reasons=discover(c,shared,slot_indices(slots))
    picked=spread(choices,max_slots)
    if not picked:
        raise BatchError("no valid animations discovered in the requested slot range")
    trig=Path(trig_path).read_bytes()
    ik=Path(ik_path).read_bytes()
    if len(trig)!=0x2800 or len(ik)!=0x1F2:
        raise BatchError("trigonometric and IK table blobs have invalid sizes")
    if spline_path and Path(spline_path).stat().st_size!=0x660:
        raise BatchError("spline table must be exactly 0x660 bytes")
    report=execute(picked,character_path=Path(character_path),
                   shared_path=Path(shared_path) if shared_path else None,
                   spline_path=Path(spline_path) if spline_path else None,
                   trig=trig,ik=ik,exporter=exporter,solver=solver,
                   upstream=upstream,max_frames=max_frames,
                   strict_complete=strict_complete)
    report.update({"format":1,"bank_sha256":c["sha256"],
                  "shared_sha256":shared["sha256"] if shared else None,
                  "discovered_unique_animations":len(choices),
                  "discovery_skips":dict(sorted(discovery_reasons.items())),
                  "duplicate_slots":sum(i["duplicate_slots"] for i in choices)})
    report["pass"]=report["pass"] and (not fail_on_skip or
        not report["skipped_during_execution"])
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bank",type=Path,required=True)
    p.add_argument("--common",type=Path,help="private common bank (kind 99)")
    p.add_argument("--spline",type=Path,help="private normalized 0x660-byte table")
    p.add_argument("--trig",type=Path,required=True)
    p.add_argument("--ik",type=Path,required=True)
    p.add_argument("--exporter",type=Path,default=Path("build/host/t3_motion_export"))
    p.add_argument("--solver",type=Path,default=Path("build/host/t3_pose_eval"))
    p.add_argument("--upstream",type=Path,
        default=Path("third_party/Tekken3Remake/tools/research/pose.py"))
    p.add_argument("--slots",help="optional comma-separated slots/ranges, e.g. 0-30,100,500-510")
    p.add_argument("--max-slots",type=int,default=24)
    p.add_argument("--max-frames",type=int,default=32)
    p.add_argument("--strict-complete",action="store_true")
    p.add_argument("--fail-on-skip",action="store_true")
    p.add_argument("--report",type=Path,help="private JSON summary; written atomically")
    args=p.parse_args()
    try:
        report=batch(args.bank,args.common,args.spline,args.trig,args.ik,
                     args.exporter,args.solver,args.upstream,
                     slots=args.slots,max_slots=args.max_slots,
                     max_frames=args.max_frames,
                     strict_complete=args.strict_complete,
                     fail_on_skip=args.fail_on_skip)
        payload=json.dumps(report,indent=2,sort_keys=True)+"\n"
        if args.report:
            args.report.parent.mkdir(parents=True,exist_ok=True)
            candidate=args.report.with_name(args.report.name+".tmp")
            candidate.write_text(payload,encoding="utf8")
            candidate.replace(args.report)
        print(payload,end="")
        return 0 if report["pass"] else 1
    except (OSError,BatchError,DifferentialError) as exc:
        p.exit(2,f"Private DIVMOT batch validation failed: {exc}\n")

if __name__=="__main__":
    raise SystemExit(main())
