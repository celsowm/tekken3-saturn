#!/usr/bin/env python3
"""Code size of chosen sources at several optimization levels.

Takes each source's real compile command from a configured Ninja build,
appends each level (the last -O wins) and prints the object's .text size, so
a High WRAM saving can be weighed before a per-source option is set.

    scripts/source_size_by_opt.py BUILD psx_ai.c psx_camera.c [--opt -O3 -O2 -Os]
"""
import argparse
import shlex
import subprocess
import tempfile
from pathlib import Path


def commands(build):
    out = subprocess.run(['ninja', '-C', str(build), '-t', 'commands'],
                         capture_output=True, text=True, check=True).stdout
    return [line for line in out.splitlines() if ' -c ' in line]


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('build', type=Path)
    p.add_argument('sources', nargs='+', help='source path suffixes, e.g. src/game/psx_ai.c')
    p.add_argument('--opt', nargs='+', default=['-O3', '-O2', '-Os'])
    p.add_argument('--size', default=str(Path.home() / 'dev/saturn-tools/bin/sh2eb-elf-size'))
    args = p.parse_args()
    lines = commands(args.build)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / 'probe.o'
        for source in args.sources:
            found = [l for l in lines if f'/{source} ' in l + ' ' or l.endswith('/' + source)]
            if not found:
                raise SystemExit(f'{source}: no compile command in {args.build}')
            words = shlex.split(found[0])
            if words[:2] == ['cd', str(words[1])] and '&&' in words:
                cwd, words = words[1], words[words.index('&&') + 1:]
            else:
                cwd = str(args.build)
            at = words.index('-o')
            sizes = []
            for level in args.opt:
                command = words[:at] + ['-o', str(out)] + words[at + 2:] + [level]
                subprocess.run(command, cwd=cwd, check=True)
                text = subprocess.run([args.size, str(out)], capture_output=True, text=True,
                                      check=True).stdout.splitlines()[1].split()[0]
                sizes.append(f'{level} {text}')
            print(source, '  '.join(sizes))


if __name__ == '__main__':
    main()
