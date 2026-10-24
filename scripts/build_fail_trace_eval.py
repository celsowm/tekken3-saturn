#!/usr/bin/env python3
"""Build a diagnostic evaluator whose refusals name their source line.

Copies src/game to a scratch directory, rewrites every `return 0;` into a call
that prints `file:line` to stderr before returning 0, and links the copy with
the configured private inputs of a host build (build/host by default). The
target is t3_match_replay_eval unless --target names another host executable
(for example t3_match_camera_eval).
The source tree is not modified. Run the result like the target: the
last lines on stderr before a refused tick show the chain of failed checks,
innermost first.
"""
import argparse
import re
import shlex
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRACE = '''#ifndef T3_FAIL_TRACE_H
#define T3_FAIL_TRACE_H
#include <stdio.h>
static inline void t3_fail_trace(const char* file,int line){fprintf(stderr,"refused %s:%d\\n",file,line);}
#endif
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=Path, default=ROOT / 'build/host')
    p.add_argument('--work', type=Path, default=ROOT / 'build/fail-trace')
    p.add_argument('--target', default='t3_match_replay_eval')
    p.add_argument('--output', type=Path, help='default: build/fail-trace/<target>')
    args = p.parse_args()
    args.output = args.output or ROOT / 'build/fail-trace' / args.target
    commands = subprocess.run(['ninja', '-C', str(args.host), '-t', 'commands', args.target],
                              capture_output=True, text=True, check=True).stdout.splitlines()
    sources = args.work / 'game'
    sources.mkdir(parents=True, exist_ok=True)
    (sources / 't3_fail_trace.h').write_text(TRACE)
    for path in sorted((ROOT / 'src/game').iterdir()):
        text = path.read_text()
        if path.suffix == '.c':
            name = path.name
            text = '#include "t3_fail_trace.h"\n#line 1\n' + re.sub(
                r'\breturn 0;', f'{{t3_fail_trace("{name}",__LINE__);return 0;}}', text)
        if path.suffix in ('.c', '.h', '.inc'):
            (sources / path.name).write_text(text)
    objects = []
    for line in commands:
        words = shlex.split(line)
        if not words or words[0] != '/usr/bin/cc' or '-c' not in words:
            continue
        source = Path(words[words.index('-c') + 1])
        if source.parent == ROOT / 'src/game':
            source = sources / source.name
        obj = args.work / (source.stem + '.o')
        flags = [w if w != f'-I{ROOT}/src/game' else f'-I{sources}' for w in words[1:]
                 if w not in ('-MD',)]
        cut = []
        skip = False
        for w in flags:
            if skip:
                skip = False
                continue
            if w in ('-MT', '-MF', '-o', '-c'):
                skip = True
                continue
            cut.append(w)
        subprocess.run(['/usr/bin/cc', *cut, '-I', str(sources), '-o', str(obj), '-c', str(source)], check=True)
        objects.append(str(obj))
    subprocess.run(['/usr/bin/cc', '-O1', '-o', str(args.output), *objects], check=True)
    print(args.output)


if __name__ == '__main__':
    main()
