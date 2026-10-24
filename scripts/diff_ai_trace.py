#!/usr/bin/env python3
"""Compare two CPU-opponent read traces of one frame.

The original's comes from fight_oracle_export.py --ai-trace FRAME
(ai-trace.txt), ours from t3_match_replay_eval with T3_AI_TRACE=FRAME and
T3_AI_TRACE_OUT. Lines are "A fighter", "R address size value" and
"C row 0 result". The traces are aligned by address and size (difflib), so
the first reported line is where the AI read a different value or took a
different path.
"""
import argparse
import difflib
from pathlib import Path


# The original's stack (spilled registers) is not on our bus.
STACK = 0x801E0000


def load(path):
    """Lines with numbers as ints; unicorn reports physical addresses, so
    KSEG0 is restored."""
    out = []
    for line in Path(path).read_text().splitlines():
        x = line.split()
        if not x:
            continue
        if x[0] == 'A':
            out.append(('A', int(x[1])))
            continue
        address = int(x[1], 16)
        if address < 0x20000000:
            address |= 0x80000000
        if x[0] == 'R' and address >= STACK:
            continue
        out.append((x[0], address, int(x[2]), int(x[3], 16)))
    return out


def text(x):
    return ' '.join(f'{v:#x}' if isinstance(v, int) and k in (1, 3) else str(v) for k, v in enumerate(x))


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('original', type=Path)
    p.add_argument('native', type=Path)
    p.add_argument('--show', type=int, default=20, help='differences to print')
    a = p.parse_args()
    original, native = load(a.original), load(a.native)
    key = lambda line: line[:3] if line[0] != 'A' else line
    matcher = difflib.SequenceMatcher(None, [key(x) for x in original], [key(x) for x in native], autojunk=False)
    shown = 0
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == 'equal':
            for i, j in zip(range(i1, i2), range(j1, j2)):
                if original[i] != native[j] and shown < a.show:
                    print(f'value  #{i}/{j}: original {text(original[i])}, ours {text(native[j])}')
                    shown += 1
        elif shown < a.show:
            print(f'{tag:6} original #{i1}-{i2}: {[text(x) for x in original[i1:min(i2, i1 + 4)]]}')
            print(f'{"":6} ours     #{j1}-{j2}: {[text(x) for x in native[j1:min(j2, j1 + 4)]]}')
            shown += 1
    print(f'{len(original)} original lines, {len(native)} ours, similarity {matcher.ratio():.3f}')


if __name__ == '__main__':
    raise SystemExit(main())
