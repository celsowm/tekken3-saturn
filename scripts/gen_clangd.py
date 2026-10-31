#!/usr/bin/env python3
"""Write a .clangd so editors resolve this project's includes and macros.

The Saturn build cross-compiles with the SH-2 toolchain, so there is no
compile database an editor's clang can use. This writes the include paths
and definitions the CMake build passes, as absolute paths, into the
git-ignored .clangd at the repository root.

    python3 scripts/gen_clangd.py [--build build/roster] [--prefix DIR]
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", default="build/roster", help="CMake build directory")
    ap.add_argument("--prefix", default=os.path.expanduser("~/dev/saturn-prefix"),
                    help="installed LibSaturn prefix")
    args = ap.parse_args()

    build = os.path.join(ROOT, args.build)
    includes = [os.path.join(ROOT, d) for d in ("src", "src/game", "src/saturn", "tools", "tests")]
    generated = os.path.join(build, "generated")
    if os.path.isdir(generated):
        includes += sorted(os.path.join(generated, d) for d in os.listdir(generated)
                           if os.path.isdir(os.path.join(generated, d)))
    sdk = os.path.join(args.prefix, "include")
    if not os.path.isdir(sdk):
        sys.exit(f"gen_clangd: no LibSaturn headers at {sdk}; pass --prefix")
    includes.append(sdk)

    # The Saturn is 32-bit ELF: this keeps pointer sizes and section attributes valid.
    # -xc because a lone .h would be guessed as Objective-C++, which refuses -std=gnu11.
    flags = ["--target=i386-unknown-none-elf", "-xc", "-std=gnu11", "-DT3_FIGHTER_PACKS=1", "-DT3_SECOND_CHARACTER=1"]
    flags += [f"-I{d}" for d in includes]
    lines = ["CompileFlags:", "  Add:"] + [f"    - {f}" for f in flags]
    # The host tests run on this machine and need its C library.
    lines += ["---", "If:", "  PathMatch: tests/.*", "CompileFlags:", "  Remove: [--target=*]"]
    # These two only build with the macros their CMake options define.
    lines += ["---", "If:", "  PathMatch: .*/particle_prepare\\.c$", "CompileFlags:",
              "  Add: [-DT3_PARTICLE_IMAGE_SNAPSHOT=1, -DT3_PARTICLE_PREPARE=1]"]
    # The sources include what they use on purpose and the build, not the
    # editor, decides what is reachable, so the include-cleaner hints only add noise.
    # SH-2 inline assembly names registers the editor's stand-in target does not have.
    lines += ["---", "Diagnostics:", "  UnusedIncludes: None", "  MissingIncludes: None",
              "  Suppress: [asm_unknown_register_name]"]
    path = os.path.join(ROOT, ".clangd")
    with open(path, "w", encoding="utf-8") as out:
        out.write("\n".join(lines) + "\n")
    print(f"wrote {path} ({len(includes)} include directories)")


if __name__ == "__main__":
    main()
