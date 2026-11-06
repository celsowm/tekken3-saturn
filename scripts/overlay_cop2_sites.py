#!/usr/bin/env python3
"""List the COP2 (GTE) instruction addresses of a Tekken 3 overlay by recursive descent.

The research harness (third_party/Tekken3Remake/tools/research/psxcpu.py) neutralises GTE
instructions at the addresses a disassembler lists; scanning data for COP2 opcodes would corrupt
tables. This stands in for Ghidra's ExportCop2Sites.java when the overlay was not analysed: it
follows code from every `jal` target and every overlay-internal function pointer (prologues
`addiu sp,sp,-n`), both sides of branches, and ends a path at `jr` or `j` (plus its delay slot).

Usage: overlay_cop2_sites.py <overlay.ovl> <base> <out.cop2.txt> [--check <resident.exe.bin> <resident.cop2.txt>]

Writes the resident EXE's sites too (the harness reads one file per overlay), and with --check
runs the same walk over the resident EXE from its known sites to report how many it misses.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

TEXT_BASE = 0x80010000
EXE_HEADER = 0x800


def walk(code: bytes, base: int, seeds: set[int]) -> tuple[set[int], set[int]]:
    """Returns (instruction addresses reached, COP2 sites among them)."""
    end = base + len(code)
    words = struct.unpack_from(f"<{len(code) // 4}I", code)

    def word(a: int) -> int:
        return words[(a - base) >> 2]

    seen: set[int] = set()
    cop2: set[int] = set()
    todo = [a for a in seeds if base <= a < end and a % 4 == 0]
    while todo:
        a = todo.pop()
        while base <= a < end and a not in seen:
            w = word(a)
            seen.add(a)
            op = w >> 26
            if op in (0x12, 0x32, 0x3A):
                cop2.add(a)
            if op == 0 and (w & 0x3F) in (8, 9):                      # jr, jalr
                if (w & 0x3F) == 8:
                    seen.add(a + 4)
                    if a + 4 < end and word(a + 4) >> 26 in (0x12, 0x32, 0x3A):
                        cop2.add(a + 4)
                    break
                a += 4
                continue
            if op == 2:                                                 # j
                seen.add(a + 4)
                if a + 4 < end and word(a + 4) >> 26 in (0x12, 0x32, 0x3A):
                    cop2.add(a + 4)
                todo.append(((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
                break
            if op == 3:                                                 # jal
                todo.append(((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
            elif op in (4, 5, 6, 7) or (op == 1 and ((w >> 16) & 0x1F) in (0, 1, 16, 17)):
                off = w & 0xFFFF
                off -= 0x10000 if off & 0x8000 else 0
                todo.append(a + 4 + (off << 2))
                if op == 4 and (w >> 21) & 31 == 0 and (w >> 16) & 31 == 0:   # b: unconditional
                    seen.add(a + 4)
                    if a + 4 < end and word(a + 4) >> 26 in (0x12, 0x32, 0x3A):
                        cop2.add(a + 4)
                    break
            a += 4
    return seen, cop2


def seeds_of(code: bytes, base: int) -> set[int]:
    """jal targets and prologue-shaped function starts inside the image, plus pointers to them."""
    n = len(code) // 4
    words = struct.unpack_from(f"<{n}I", code)
    end = base + len(code)
    seeds: set[int] = set()
    for i, w in enumerate(words):
        a = base + 4 * i
        if w >> 26 == 3:
            t = ((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if base <= t < end:
                seeds.add(t)
        if (w & 0xFFFF0000) == 0x27BD0000 and w & 0x8000:               # addiu sp,sp,-n
            seeds.add(a)
        if base <= w < end and w % 4 == 0:                               # function pointer in a table
            t = words[(w - base) >> 2]
            if (t & 0xFFFF0000) == 0x27BD0000 and t & 0x8000:
                seeds.add(w)
    return seeds


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("overlay", type=Path)
    ap.add_argument("base", type=lambda s: int(s, 0))
    ap.add_argument("out", type=Path)
    ap.add_argument("--resident", type=Path, nargs=2, metavar=("EXE_BIN", "COP2_TXT"),
                    help="the resident EXE image and its site list, copied into OUT; also checked")
    args = ap.parse_args()
    code = args.overlay.read_bytes()
    code = code[: len(code) // 4 * 4]
    _, sites = walk(code, args.base, seeds_of(code, args.base))
    lines = {f"{a:08x}" for a in sites}
    if args.resident:
        exe = args.resident[0].read_bytes()[EXE_HEADER:]
        known = {int(x, 16) for x in args.resident[1].read_text().split()}
        mine = walk(exe[: len(exe) // 4 * 4], TEXT_BASE, seeds_of(exe, TEXT_BASE))[1]
        low = {a for a in known if a < args.base}
        print(f"resident: {len(low & mine)} of {len(low)} Ghidra sites found, {len(mine - known)} extra",
              file=sys.stderr)
        lines |= {f"{a:08x}" for a in low}
    args.out.write_text("\n".join(sorted(lines)) + "\n")
    print(f"{len(sites)} overlay sites -> {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
