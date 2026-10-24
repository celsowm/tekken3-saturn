#!/usr/bin/env python3
"""Compare the C CPU opponent (src/game/psx_ai.c) with the original routines.

Runs the pinned remake's AI verification cases (tools/research/verify_ai_sim.py)
with its Python port replaced by the C port: both the original code in the CPU
harness and the C code start from the same guest RAM, and the cases compare
the AI record, candidates, RNG states and pads. BranchCondition comes from the
remake's verified fight_sim port. Run it with the remake's venv:

  third_party/Tekken3Remake/work/venv/bin/python scripts/verify_psx_ai.py --cases 300
"""
import argparse
import ctypes
import logging
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
CASES = ('AiUpdate', 'AiUpdateProbe', 'AiUpdateChain', 'AiReset', 'AiInitRound')
CONDITION = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)
PROBE = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32)


def build(out: Path) -> ctypes.CDLL:
    lib = out / 'libpsx_ai_flat.so'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-I', str(ROOT / 'src/game'), '-o', str(lib),
                    str(ROOT / 'src/game/psx_ai.c'), str(ROOT / 'tools/psx_ai_flat.c')], check=True)
    dll = ctypes.CDLL(str(lib))
    dll.flat_ai_update.restype = ctypes.c_uint32
    dll.flat_ai_update.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32, CONDITION, PROBE]
    dll.flat_ai_reset.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                  CONDITION]
    dll.flat_ai_init_round.argtypes = [ctypes.c_void_p, CONDITION]
    dll.flat_ai_coverage.argtypes = [ctypes.c_void_p]
    return dll


def report_coverage(cover: bytearray, out: Path) -> None:
    """Writes the guest ranges the C AI read (1), wrote (2) or both (3):
    fighter offsets folded onto one record, everything else as merged runs."""
    import json
    base, stride = 0x800A96F0, 0x188C
    fighter = bytearray(stride)
    for k in range(2):
        start = (base & 0x1FFFFF) + k * stride
        for i in range(stride):
            fighter[i] |= cover[start + i]
            cover[start + i] = 0

    def runs(data, origin):
        found, i = [], 0
        while i < len(data):
            if not data[i]:
                i += 1
                continue
            j = i
            while j < len(data) and data[j] == data[i]:
                j += 1
            found.append([hex(origin + i), j - i, data[i]])
            i = j
        return found
    out.write_text(json.dumps({'fighter': runs(fighter, 0), 'other': runs(cover, 0x80000000)}, indent=1))


def install(dll, ai, ai_update, sim):
    """Point the remake's port entry points at the C port."""
    def callbacks(ram, probes=None):
        cond = CONDITION(lambda row, f, o: int(bool(sim.branch_condition(ram, row, sim.Fighter(ram, f),
                                                                         sim.Fighter(ram, o)))))
        prb = PROBE(lambda addr, rec: probes[addr](ram, rec) if probes and addr in probes else None)
        return cond, prb

    def buffer(ram):
        return ctypes.addressof((ctypes.c_char * len(ram.data)).from_buffer(ram.data))

    def update(ram, fighter, stale=None, probes=None):
        cond, prb = callbacks(ram, probes)
        v = dll.flat_ai_update(buffer(ram), fighter, stale is not None, stale or 0, cond, prb)
        return v & 0xFFFF, v >> 16

    def reset(ram, fighter, group, level, disabled):
        cond, _ = callbacks(ram)
        dll.flat_ai_reset(buffer(ram), fighter, group, level, disabled, cond)

    def init_round(ram):
        cond, _ = callbacks(ram)
        dll.flat_ai_init_round(buffer(ram), cond)

    ai_update.ai_update = update
    ai.ai_reset = reset
    ai.init_round = init_round


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--cases', type=int, default=300)
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--only', choices=CASES)
    p.add_argument('--coverage', type=Path, help='write the guest ranges the C AI touched (JSON)')
    args = p.parse_args()
    logging.basicConfig(level=logging.INFO, format='%(message)s')
    sys.path.insert(0, str(RESEARCH))
    import ai_sim as ai
    import ai_update
    import fight_sim as sim
    import verify_ai_sim as v
    from psxcpu import load_release
    with tempfile.TemporaryDirectory() as tmp:
        dll = build(Path(tmp))
        install(dll, ai, ai_update, sim)
        cover = bytearray(0x200000)
        if args.coverage:
            dll.flat_ai_coverage(ctypes.addressof((ctypes.c_char * len(cover)).from_buffer(cover)))
        rng = random.Random(args.seed)
        cpu = load_release()
        v.patch_harness(cpu)
        failed = False
        for name in CASES:
            if args.only and name != args.only:
                continue
            bad = sum(not v.CASES[name](cpu, rng) for _ in range(args.cases))
            logging.info('%s: %d cases, %d mismatches', name, args.cases, bad)
            failed |= bad > 0
        if args.coverage:
            dll.flat_ai_coverage(None)
            report_coverage(cover, args.coverage)
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
