#!/usr/bin/env python3
"""Build a PRIVATE logic-only DIVMOT binary from a local PS1 bank.

Keeps original section 0 (12-byte branches), 1 (56-byte moves), 2 (4023
move slots). With --with-hitboxes, also preserves section 9's authored
attack points; raw animations, cameras, sounds and models are omitted. This is a RESEARCH/PORTING intermediate, never a gameplay-ready
replacement for a bank with all data.
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

def compact(data, with_hitboxes=False):
    count,s=sections_of(data)
    kept=data[s[0]:s[3]]
    # Original section9 move descriptor offsets are section-relative. Preserve
    # its bytes untouched and relocate only the section directory itself.
    attacks=data[s[9]:s[10]] if with_hitboxes else b""
    end=64+len(kept)
    offsets=[64,64+s[1]-s[0],64+s[2]-s[0]]+[end]*7+[end+len(attacks)]*5
    result=bytearray(data[:4]+struct.pack("<15I",*offsets)+kept+attacks)
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
        "attack_geometry_included":bool(with_hitboxes),
        "omitted":"animations, textures, camera, audio, events"
                  + ("" if with_hitboxes else ", attack points"),
        "valid_for":"move VM and local attack geometry decoding only; full combat pending"
    }

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bank",type=Path,required=True,help="local extracted raw BNS DIVMOT record")
    p.add_argument("--output",type=Path,required=True,help="gitignored destination directory")
    p.add_argument("--with-hitboxes",action="store_true",
                   help="preserve local copyrighted baked attack descriptors (section 9)")
    a=p.parse_args()
    try:
        data=a.bank.read_bytes()
        logic,manifest=compact(data,with_hitboxes=a.with_hitboxes)
        a.output.mkdir(parents=True,exist_ok=True)
        (a.output/f"bank_{manifest['bank_type']}.t3logic").write_bytes(logic)
        (a.output/f"bank_{manifest['bank_type']}.json").write_text(
            json.dumps(manifest,indent=2,sort_keys=True)+"\n")
    except (OSError,FormatError) as error:
        p.exit(1,f"DIVMOT compact import failed: {error}\n")
    print(json.dumps(manifest,sort_keys=True))

if __name__=="__main__":main()
