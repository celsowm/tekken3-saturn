#!/usr/bin/env python3
"""Compare two Ymir probe cycle profiles function by function. Each side is a `--debug-profile PREFIX` run's
PREFIX.pc.csv (cpu, pc, instructions, execution_cycles) and the ELF it ran; the PCs are mapped to the ELF's
function symbols (sh2eb-elf-nm), so the two builds may lay their code out differently. Prints each side's total and
the functions whose cycles differ most.

    scripts/ymir_profile_diff.py build/yprof-a/dbg.pc.csv build/a/tekken3_force.elf \
        build/yprof-b/dbg.pc.csv build/b/tekken3_force.elf --top 30
"""
import argparse
import bisect
import csv
import os
import subprocess
from collections import defaultdict
from pathlib import Path

NM = Path.home() / 'dev/saturn-tools/bin/sh2eb-elf-nm'


def functions(elf):
    out = subprocess.check_output([str(NM) if NM.exists() else 'sh2eb-elf-nm', '-n', str(elf)], text=True,
                                  env={**os.environ})
    starts, names = [], []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in 'tTwW':
            starts.append(int(parts[0], 16))
            names.append(parts[2])
    return starts, names


def profile(path, elf, cpu):
    starts, names = functions(elf)
    cycles = defaultdict(int)
    with open(path) as f:
        for row in csv.DictReader(f):
            if row['cpu'] != cpu:
                continue
            pc = int(row['pc'])
            i = bisect.bisect_right(starts, pc) - 1
            cycles[names[i] if i >= 0 else '?'] += int(row['execution_cycles'])
    return cycles


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pc_a', type=Path)
p.add_argument('elf_a', type=Path)
p.add_argument('pc_b', type=Path)
p.add_argument('elf_b', type=Path)
p.add_argument('--cpu', default='master')
p.add_argument('--top', type=int, default=25)
a = p.parse_args()

ca = profile(a.pc_a, a.elf_a, a.cpu)
cb = profile(a.pc_b, a.elf_b, a.cpu)
print(f'total cycles: a {sum(ca.values())}  b {sum(cb.values())}  b-a {sum(cb.values()) - sum(ca.values())}')
rows = sorted(set(ca) | set(cb), key=lambda n: -abs(cb.get(n, 0) - ca.get(n, 0)))
for name in rows[:a.top]:
    print(f'{cb.get(name, 0) - ca.get(name, 0):+12d}  a {ca.get(name, 0):11d}  b {cb.get(name, 0):11d}  {name}')
