#!/usr/bin/env python3
"""Show one function's disassembly from two ELFs, for a compiler comparison.

    scripts/compare_elf_function.py OLD.elf NEW.elf SYMBOL [--full]

SYMBOL is the C name (a leading underscore is added when the ELF uses one).
Prints the instruction count of each and, with --full, both listings with
addresses stripped so they can be diffed by eye. Uses sh2eb-elf-objdump and
sh2eb-elf-nm from PATH.
"""
import argparse
import subprocess


def listing(elf, symbol):
    nm = subprocess.run(["sh2eb-elf-nm", "-S", elf], check=True,
                        capture_output=True, text=True).stdout
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] in (symbol, "_" + symbol):
            start, size = int(parts[0], 16), int(parts[1], 16)
            break
    else:
        raise SystemExit(f"{symbol} not in {elf}")
    out = subprocess.run(["sh2eb-elf-objdump", "-d", "--no-show-raw-insn",
                          f"--start-address={start:#x}", f"--stop-address={start + size:#x}", elf],
                         check=True, capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        if ":\t" in line and not line.endswith(">:"):
            rows.append(line.split(":\t", 1)[1].strip())
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("symbol")
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    a, b = listing(args.old, args.symbol), listing(args.new, args.symbol)
    print(f"{args.symbol}: {len(a)} -> {len(b)} instructions")
    if args.full:
        width = max((len(x) for x in a), default=0) + 2
        for i in range(max(len(a), len(b))):
            print(f"{(a[i] if i < len(a) else ''):<{width}}{b[i] if i < len(b) else ''}")


if __name__ == "__main__":
    main()
