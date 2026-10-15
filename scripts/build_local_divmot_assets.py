#!/usr/bin/env python3
"""Embed PRIVATE compact DIVMOT banks into a local Saturn diagnostic build.

Never use original Namco banks or this generated C in public CI/releases.
"""
import argparse
from pathlib import Path
from divmot_to_logic import sections_of

def generate(bank_path,common_path,out):
    bank=bank_path.read_bytes()
    _,section=sections_of(bank)
    if bank[1]==99:raise ValueError("main bank must be character bank, not type 99")
    common=common_path.read_bytes() if common_path else b""
    if common:
        sections_of(common)
        if common[1]!=99:raise ValueError("common bank must be divmot99")
    if len(bank)+len(common)>450000:
        raise ValueError("logic banks exceed 450-kilobyte experimental budget")
    out.mkdir(parents=True,exist_ok=True)
    header=f"""#ifndef T3_LOCAL_DIVMOT_H
#define T3_LOCAL_DIVMOT_H
#include <stdint.h>
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
          +emit("t3_local_bank",bank)+emit("t3_local_common",common))
    (out/"t3_local_divmot.h").write_text(header)
    (out/"t3_local_divmot.c").write_text(impl)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bank",type=Path,required=True)
    p.add_argument("--common",type=Path)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    try:generate(a.bank,a.common,a.output)
    except (OSError,ValueError) as e:p.exit(1,f"private DIVMOT embed failed: {e}\n")
if __name__=="__main__":main()
