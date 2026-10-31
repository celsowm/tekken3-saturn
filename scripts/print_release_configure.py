#!/usr/bin/env python3
"""Print the CMake configure command that reproduces a Saturn build directory.

Reads the build's CMakeCache.txt and keeps every T3_* option, including empty,
OFF and zero values, plus the toolchain and SDK paths, so the command for the
Paul-against-Law release image is whatever `build/t30-law` actually used:

    python3 scripts/print_release_configure.py build/t30-law build/release
"""
import shlex
import sys
from pathlib import Path

KEEP = ('CMAKE_BUILD_TYPE', 'CMAKE_TOOLCHAIN_FILE', 'CMAKE_PREFIX_PATH', 'LibSaturn_DIR',
        'LIBSATURN_SOURCE_DIR', 'LIBSATURN_MKISOFS')


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    cache, target = Path(sys.argv[1]) / 'CMakeCache.txt', sys.argv[2]
    args = []
    for line in cache.read_text().splitlines():
        if not line or line.startswith(('#', '//')) or '=' not in line or ':' not in line:
            continue
        key, rest = line.split(':', 1)
        kind, value = rest.split('=', 1)
        if (key in KEEP or key.startswith('T3_')) and kind in ('BOOL', 'STRING', 'PATH', 'FILEPATH', 'UNINITIALIZED'):
            if kind == 'UNINITIALIZED':
                kind = 'STRING'
            args.append(f'-D{key}:{kind}={value}')
    print('cmake -S . -B', shlex.quote(target), '-G Ninja \\\n  ' + ' \\\n  '.join(shlex.quote(a) for a in args))


if __name__ == '__main__':
    main()
