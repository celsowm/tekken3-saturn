#!/usr/bin/env python3
"""Write a CMake initial cache (-C) holding a build directory's options.

Copies every T3_*, SAT_* and LIBSATURN_* entry plus the build type, toolchain
file and prefix path from BUILD/CMakeCache.txt, so a fresh directory (for
example one configured with another compiler) gets the same configuration:

    scripts/cmake_options_cache.py build/perf-n3-simprof > opts.cmake
    PATH=/new/toolchain/bin:$PATH cmake -S . -B build/new -C opts.cmake
"""
import argparse
import pathlib
import re

KEEP = re.compile(r"^(T3_|SAT_|LIBSATURN_)|^(CMAKE_BUILD_TYPE|CMAKE_TOOLCHAIN_FILE|CMAKE_PREFIX_PATH)$")
ENTRY = re.compile(r"^([A-Za-z0-9_.+-]+):([A-Z]+)=(.*)$")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build", type=pathlib.Path)
    args = parser.parse_args()
    for line in (args.build / "CMakeCache.txt").read_text().splitlines():
        m = ENTRY.match(line)
        if not m or not KEEP.search(m.group(1)) or m.group(2) in ("INTERNAL", "STATIC"):
            continue
        name, kind, value = m.groups()
        value = value.replace("\\", "\\\\").replace('"', '\\"')
        print(f'set({name} "{value}" CACHE {kind} "")')


if __name__ == "__main__":
    main()
