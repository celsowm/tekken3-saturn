#!/usr/bin/env python3
"""Fields the original AiUpdate read in one call that the owned bus reads differently: for every (address, size) the
first value each trace has (the original's trace holds the first call only; addresses are Rev.1 masked to 24 bits).

    python3 scripts/ai_first_diff.py original.txt ours.txt     # from force_ai_trace.py and T3_AI_TRACE_OUT
"""
import sys


def reads(text):
    out = []
    for line in text.splitlines():
        t = line.split()
        if t and t[0] == 'R':
            out.append((int(t[1], 16) & 0xFFFFFF, int(t[2]), int(t[3], 16)))
    return out


original = reads(open(sys.argv[1]).read().split('\nA 2')[0])
ours = reads(open(sys.argv[2]).read())
first_o, first_u = {}, {}
for a, s, v in original:
    first_o.setdefault((a, s), v)
for a, s, v in ours:
    first_u.setdefault((a, s), v)
for key in first_o:
    if key in first_u and first_o[key] != first_u[key]:
        print(f'{key[0]:#x} size {key[1]}: original {first_o[key]:#x}, ours {first_u[key]:#x}')
print(f'{len(original)} reads (original), {len(ours)} (ours); original-only {len([k for k in first_o if k not in first_u])}')
