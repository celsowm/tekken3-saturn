#!/usr/bin/env python3
"""Per-function and per-instruction SH-2 costs from a Ymir debug profile.

Reads the debug.pc.csv that `saturn_probe.py --debug-profile` writes (one row
per CPU and PC: instructions and execution cycles over the traced frames)
and the matching ELF. No retail data needed.

    functions:     the most expensive functions per CPU, with entry counts
                   (a function's entry address executed) and cycles per call
                   of a reference function (one rendered image by default)
    instructions:  one function's disassembly with cycles per frame on each
                   instruction, and optionally the hottest blocks of it
    callers:       the executed call sites of one function per CPU (calls per
                   frame), for helpers such as memcpy or libgcc shifts that
                   compiled code reaches through a register loaded from a
                   literal pool

    saturn_pc_profile.py functions PROFILE/debug.pc.csv build/X/tekken3_saturn.elf
    saturn_pc_profile.py instructions PROFILE/debug.pc.csv ELF t3_stage_backdrop_lines --blocks 128
    saturn_pc_profile.py callers PROFILE/debug.pc.csv ELF memcpy --cpu slave
"""
import argparse
import bisect
import collections
import csv
import re
import subprocess
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2]/'saturn-tools/bin'
IMAGE = 'saturn::core::render3d::project_vertices'


def rows(path):
    for r in csv.DictReader(open(path)):
        yield r['cpu'], int(r['pc']) & ~0x20000000, int(r['instructions']), int(r['execution_cycles'])


def functions(a):
    out = subprocess.run([str(a.nm), '-n', '-C', str(a.elf)], capture_output=True, text=True, check=True).stdout
    symbols = []
    for line in out.splitlines():
        p = line.split(None, 2)
        if len(p) == 3 and p[1] in 'tTwW':
            symbols.append((int(p[0], 16), p[2]))
    starts = [s for s, _ in symbols]
    entry = {s: n for s, n in symbols}
    cycles = collections.defaultdict(collections.Counter)
    calls = collections.defaultdict(collections.Counter)
    for cpu, pc, n, c in rows(a.csv):
        i = bisect.bisect_right(starts, pc)-1
        name = symbols[i][1] if i >= 0 else '?'
        cycles[cpu][name] += c
        if entry.get(pc) == name:
            calls[cpu][name] += n
    for cpu in sorted(cycles):
        total = sum(cycles[cpu].values())
        unit = max([v for k, v in calls[cpu].items() if k.startswith(a.per)] or [1])
        print(f'== {cpu} {total} per frame {total//a.frames} per-call {unit} cycles/per-call {total//unit}')
        for name, v in cycles[cpu].most_common(a.top):
            print(f'{v/total*100:5.1f}% {v//unit:8d}/call {calls[cpu][name]:7d} calls {name[:110]}')


def instructions(a):
    dis = subprocess.run([str(a.objdump), '-d', '-C', '--no-show-raw-insn', str(a.elf)],
                         capture_output=True, text=True, check=True).stdout
    text, order, current = {}, [], None
    for line in dis.splitlines():
        m = re.match(r'^([0-9a-f]+) <(.*)>:$', line)
        if m:
            current = m.group(2)
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(.*)$', line)
        if m and current and current.startswith(a.function):
            pc = int(m.group(1), 16)
            text[pc] = m.group(2)
            order.append(pc)
    if not order:
        raise SystemExit(f'no function starting with {a.function!r}')
    cycles, count = collections.Counter(), collections.Counter()
    for cpu, pc, n, c in rows(a.csv):
        if cpu == a.cpu and pc in text:
            cycles[pc] += c
            count[pc] += n
    total = sum(cycles.values())
    print(f'total {total} per frame {total//a.frames}')
    for pc in order:
        if cycles[pc]:
            print(f'{pc:08x} {cycles[pc]//a.frames:6d} {count[pc]//a.frames:6d}  {text[pc][:90]}')
    if a.blocks:
        blocks = collections.Counter()
        for pc in order:
            blocks[pc//a.blocks*a.blocks] += cycles[pc]
        print('-- blocks (cycles per frame, first instruction)')
        for block, c in blocks.most_common(a.top):
            first = min(pc for pc in order if pc >= block)
            print(f'{block:08x} {c//a.frames:6d}  {text[first][:60]}')


def callers(a):
    """Call sites of a.function: a jsr through a register whose last load in
    the same function named it, or a bsr straight to it."""
    dis = subprocess.run([str(a.objdump), '-d', '-C', '--no-show-raw-insn', str(a.elf)],
                         capture_output=True, text=True, check=True).stdout
    target = re.compile(r'<_?' + re.escape(a.function) + r'>')
    sites, current, loaded = {}, None, {}
    for line in dis.splitlines():
        m = re.match(r'^([0-9a-f]+) <(.*)>:$', line)
        if m:
            current, loaded = m.group(2), {}
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*([^!]*)(!.*)?$', line)
        if not m or not current:
            continue
        pc, op, args, note = int(m.group(1), 16), m.group(2), m.group(3).strip(), m.group(4) or ''
        dest = args.rsplit(',', 1)[-1].strip() if ',' in args else None
        if op == 'jsr' and loaded.get(args.strip('@')):
            sites[pc] = current
        elif op == 'bsr' and target.search(args):
            sites[pc] = current
        if dest:
            loaded[dest] = bool(op == 'mov.l' and target.search(note))
    calls = collections.Counter()
    for cpu, pc, n, c in rows(a.csv):
        if cpu == a.cpu and pc in sites:
            calls[(sites[pc], pc)] += n
    total = sum(calls.values())
    print(f'{a.function} on {a.cpu}: {total/a.frames:.1f} calls per frame from {len(calls)} sites')
    for (name, pc), n in calls.most_common(a.top):
        print(f'{n/a.frames:8.1f}  {pc:08x}  {name[:100]}')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    f = sub.add_parser('functions')
    f.add_argument('--per', default=IMAGE, help='function whose entry count divides the costs')
    f.add_argument('--nm', type=Path, default=TOOLS/'sh2eb-elf-nm')
    i = sub.add_parser('instructions')
    i.add_argument('--cpu', default='master', choices=('master', 'slave'))
    i.add_argument('--blocks', type=int, help='also sum cycles over blocks of this many bytes')
    i.add_argument('--objdump', type=Path, default=TOOLS/'sh2eb-elf-objdump')
    c = sub.add_parser('callers')
    c.add_argument('--cpu', default='master', choices=('master', 'slave'))
    c.add_argument('--objdump', type=Path, default=TOOLS/'sh2eb-elf-objdump')
    for s in (f, i, c):
        s.add_argument('csv', type=Path, help='debug.pc.csv')
        s.add_argument('elf', type=Path)
        s.add_argument('--frames', type=int, default=30, help='traced frames')
        s.add_argument('--top', type=int, default=30)
    i.add_argument('function', help='function name prefix (demangled)')
    c.add_argument('function', help='callee symbol, such as memcpy or __ashlsi3_r0')
    a = p.parse_args()
    {'functions': functions, 'instructions': instructions, 'callers': callers}[a.command](a)


if __name__ == '__main__':
    main()
