#!/usr/bin/env python3
"""Build a PRIVATE logic-only DIVMOT binary from a local PS1 bank.

Keeps sections 0 (branches), 1 (moves), 2 (move slots). Optional
--with-motion preserves raw animation section 6 (including tags);
--with-hitboxes preserves baked hitbox section 9. Other copyrighted
streams are dropped. All output stays local, not in Git or CI.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SECTIONS=15
SLOTS=4023
ROW_BYTES=56
BRANCH_BYTES=12

class FormatError(ValueError): pass

def sections_of(data):
    if len(data)<64 or len(data)>4*1024*1024 or data[0]!=0 or data[1] not in tuple(range(21))+(99,):
        raise FormatError("not a known raw DIVMOT bank")
    moves=struct.unpack_from("<H",data,2)[0]
    offsets=struct.unpack_from("<15I",data,4)
    if not moves or offsets[0]!=64 or offsets[-1]!=len(data) or any(
        offsets[i]>offsets[i+1] or offsets[i]<64 for i in range(14)):
        raise FormatError("invalid bank section directory")
    if ((offsets[1]-offsets[0])%BRANCH_BYTES or
        (offsets[2]-offsets[1])!=moves*ROW_BYTES):
        raise FormatError("invalid branch or move rows")
    if data[1]==99:
        if offsets[3]!=offsets[2]:raise FormatError("common bank has nonempty slot index")
    elif offsets[3]-offsets[2]!=SLOTS*4:
        raise FormatError("character bank must have exactly 4023 move slots")
    return moves,offsets

def compact(data, with_hitboxes=False, with_motion=False):
    count,s=sections_of(data)
    kept=data[s[0]:s[3]]
    # Sections 6 and 9 use section-relative offsets inside move rows, so
    # the original bytes can be preserved independently. Do not relocate
    # internal stream offsets or non-selected asset streams.
    motion=data[s[6]:s[7]] if with_motion else b""
    attacks=data[s[9]:s[10]] if with_hitboxes else b""
    end=64+len(kept)
    end_motion=end+len(motion)
    offsets=[64,64+s[1]-s[0],64+s[2]-s[0]] + [end]*4 + \
            [end_motion]*3 + [end_motion+len(attacks)]*5
    result=bytearray(data[:4]+struct.pack("<15I",*offsets)+kept+motion+attacks)
    assert len(result)==offsets[-1]
    sections_of(result)
    return bytes(result),{
        "source_sha256":hashlib.sha256(data).hexdigest(),
        "output_sha256":hashlib.sha256(result).hexdigest(),
        "bank_type":data[1],
        "move_rows":count,
        "branch_rows":(s[1]-s[0])//BRANCH_BYTES,
        "slot_count":0 if data[1]==99 else SLOTS,
        "original_bytes":len(data),
        "logic_bytes":len(result),
        "hitbox_bytes":len(attacks),
        "motion_bytes":len(motion),
        "motion_streams_included":bool(with_motion),
        "attack_geometry_included":bool(with_hitboxes),
        "omitted":"textures, camera, audio, events"
                  + ("" if with_motion else ", animations")
                  + ("" if with_hitboxes else ", attack points"),
        "valid_for":"move VM and optional raw pose/attack decoding; full combat pending"
    }

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bank",type=Path,required=True,help="local extracted raw BNS DIVMOT record")
    p.add_argument("--output",type=Path,required=True,help="gitignored destination directory")
    p.add_argument("--with-motion",action="store_true",
                   help="preserve original animation section 6 (private research)")
    p.add_argument("--with-hitboxes",action="store_true",
                   help="preserve local copyrighted baked attack descriptors (section 9)")
    a=p.parse_args()
    try:
        data=a.bank.read_bytes()
        logic,manifest=compact(data,with_hitboxes=a.with_hitboxes,with_motion=a.with_motion)
        a.output.mkdir(parents=True,exist_ok=True)
        (a.output/f"bank_{manifest['bank_type']}.t3logic").write_bytes(logic)
        (a.output/f"bank_{manifest['bank_type']}.json").write_text(
            json.dumps(manifest,indent=2,sort_keys=True)+"\n")
    except (OSError,FormatError) as error:
        p.exit(1,f"DIVMOT compact import failed: {error}\n")
    print(json.dumps(manifest,sort_keys=True))

if __name__=="__main__":main()
