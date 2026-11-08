#!/usr/bin/env python3
"""Compare per-symbol code size between two ELFs (for example two compilers).

    scripts/compare_elf_sizes.py OLD.elf NEW.elf [--top N] [--match TEXT ...]

Prints the symbols whose size changed most, old size, new size and name
(demangled). --match keeps only names containing one of the texts. Uses
sh2eb-elf-nm from PATH unless --nm is given.
"""
import argparse
import subprocess


def sizes(nm, elf):
    out = subprocess.run([nm, "-S", "-C", "--defined-only", elf], check=True,
                         capture_output=True, text=True).stdout
    result = {}
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[2] in "tTwW":
            result[parts[3]] = result.get(parts[3], 0) + int(parts[1], 16)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--match", nargs="*", default=[])
    parser.add_argument("--nm", default="sh2eb-elf-nm")
    args = parser.parse_args()
    a, b = sizes(args.nm, args.old), sizes(args.nm, args.new)
    names = [n for n in set(a) | set(b) if not args.match or any(m in n for m in args.match)]
    names.sort(key=lambda n: -abs(b.get(n, 0) - a.get(n, 0)))
    print(f"text {sum(a.values())} -> {sum(b.values())}")
    for n in names[:args.top]:
        print(f"{b.get(n, 0) - a.get(n, 0):+7d} {a.get(n, 0):7d} -> {b.get(n, 0):7d}  {n[:100]}")


if __name__ == "__main__":
    main()
