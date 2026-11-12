#!/usr/bin/env python3
"""Read Saturn Work RAM from a Mednafen save state and dump ELF symbols.

record_mednafen.py --save-state keeps the state Mednafen writes at exit
(mednafen-profile/mcs/*.mca). The state is gzip: a 32-byte header, then
sections (32-byte name, little-endian size, body); a body is a list of
entries (name length, name, little-endian size, data). MAIN holds WorkRAML
and WorkRAMH. Mednafen keeps them as 16-bit words in host order, so the byte
order is taken from the one that matches the ELF's .text section.

    scripts/mednafen_state.py RUN_DIR --elf build/t30-law/tekken3_saturn.elf \
        --symbol _g_t3_ai_ticks --symbol _g_local_pose_cache [--words]

Each symbol prints its address, size and big-endian 32-bit words (or hex).
--cpus prints each SH-2's PC and PR (sections SH2-M and SH2-S: R is R0-R15,
CtrlRegs SR, GBR, VBR and SysRegs MACH, MACL, PR), with the ELF function around each.
"""
import argparse
import gzip
import json
import struct
import subprocess
from pathlib import Path

LOW, HIGH = 0x00200000, 0x06000000
SIZE = 0x100000


def sections(data):
    at = 32
    while at + 36 <= len(data):
        name = data[at:at + 32].split(b'\0')[0].decode(errors='replace')
        size = struct.unpack_from('<I', data, at + 32)[0]
        yield name, data[at + 36:at + 36 + size]
        at += 36 + size


def entries(body):
    at = 0
    while at < len(body):
        n = body[at]
        name = body[at + 1:at + 1 + n].split(b'\0')[0].decode(errors='replace')
        size = struct.unpack_from('<I', body, at + 1 + n)[0]
        yield name, body[at + 5 + n:at + 5 + n + size]
        at += 5 + n + size


def swap16(raw):
    out = bytearray(raw)
    out[0::2], out[1::2] = raw[1::2], raw[0::2]
    return bytes(out)


def work_ram(state_path):
    data = gzip.decompress(Path(state_path).read_bytes())
    if data[:8] != b'MDFNSVST':
        raise ValueError(f'{state_path}: not a Mednafen save state')
    for name, body in sections(data):
        if name == 'MAIN':
            ram = dict(entries(body))
            return ram['WorkRAML'], ram['WorkRAMH']
    raise ValueError(f'{state_path}: no MAIN section')


def cpus(state_path):
    """{'SH2-M': (pc, pr, sp), 'SH2-S': ...} from the state's CPU sections (host-order 32-bit words)."""
    data = gzip.decompress(Path(state_path).read_bytes())
    out = {}
    for name, body in sections(data):
        if name in ('SH2-M', 'SH2-S'):
            entry = dict(entries(body))
            regs = struct.unpack('<16I', entry['R'][:64])
            pr = struct.unpack('<3I', entry['SysRegs'][:12])[2]
            out[name] = (struct.unpack('<I', entry['PC'][:4])[0], pr, regs[15])
    return out


def function_at(table, address):
    """The ELF symbol containing address, as 'name+offset'."""
    address &= 0x0FFFFFFF
    best = None
    for name, (start, size) in table.items():
        if start <= address < start + max(size, 1) and (best is None or start > best[1]):
            best = (name, start)
    return f'{best[0]}+{address - best[1]:#x}' if best else '?'


def tool(tools, name):
    path = tools / name
    return str(path) if path.exists() else name


def elf_text(elf, tools):
    """The start of .text, which never changes, to fix the byte order."""
    out = subprocess.run([tool(tools, 'sh2eb-elf-objdump'), '-h', str(elf)],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[1] == '.text':
            size, address, offset = int(parts[2], 16), int(parts[3], 16), int(parts[5], 16)
            with open(elf, 'rb') as f:
                f.seek(offset)
                return address, f.read(min(size, 4096))
    raise ValueError('no .text section')


def symbols(elf, tools):
    out = subprocess.run([tool(tools, 'sh2eb-elf-nm'), '-S', str(elf)],
                         capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            table[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
    return table


class Memory:
    def __init__(self, state, elf, tools, text_diff=False):
        low, high = work_ram(state)
        address, expected = elf_text(elf, tools)
        at = (address & 0x0FFFFFFF) - HIGH
        if high[at:at + len(expected)] == expected:
            self.order = 'big'
        elif swap16(high[at:at + len(expected)]) == expected:
            self.order, low, high = 'swapped16', swap16(low), swap16(high)
        elif text_diff:
            # A run whose code was overwritten: keep the order with fewer differing bytes and say where they are.
            def differing(ram):
                return [i for i in range(len(expected)) if ram[at + i] != expected[i]]
            big, swapped = differing(high), differing(swap16(high))
            if len(swapped) < len(big):
                self.order, low, high, bad = 'swapped16 (text differs)', swap16(low), swap16(high), swapped
            else:
                self.order, bad = 'big (text differs)', big
            print(f'.text: {len(bad)} of the first {len(expected)} bytes differ, '
                  f'{address + bad[0]:#010x}..{address + bad[-1]:#010x}')
        else:
            raise ValueError('Work RAM does not hold this ELF: .text differs in both byte orders')
        self.low, self.high = low, high

    def read(self, address, size):
        address &= 0x0FFFFFFF
        for base, ram in ((LOW, self.low), (HIGH, self.high)):
            if base <= address and address + size <= base + SIZE:
                return ram[address - base:address - base + size]
        raise ValueError(f'{address:#x}+{size} is outside Work RAM')


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('run', type=Path, help='record_mednafen.py output, or an .mca file')
    p.add_argument('--elf', type=Path, default=Path('build/t30-law/tekken3_saturn.elf'))
    p.add_argument('--tools', type=Path, default=Path.home() / 'dev/saturn-tools/bin')
    p.add_argument('--symbol', action='append', default=[])
    p.add_argument('--words', action='store_true', help='print big-endian 32-bit words')
    p.add_argument('--json', action='store_true')
    p.add_argument('--dump', type=Path, help='also write each symbol\'s raw bytes to DIR/<symbol>.bin')
    p.add_argument('--cpus', action='store_true', help='print each SH-2\'s PC and PR with their functions')
    p.add_argument('--text-diff', action='store_true',
                   help='read the state even when the ELF\'s code differs, and report where it does')
    args = p.parse_args()
    state = args.run
    if state.is_dir():
        found = sorted((state / 'mednafen-profile/mcs').glob('*.mca'))
        if not found:
            raise SystemExit(f'{state}: no save state; record with --save-state')
        state = found[-1]
    memory = Memory(state, args.elf, args.tools, args.text_diff)
    table = symbols(args.elf, args.tools)
    result = {}
    for name in args.symbol:
        if name not in table:
            raise SystemExit(f'{name}: not in {args.elf}')
        address, size = table[name]
        raw = memory.read(address, size)
        if args.dump:
            args.dump.mkdir(parents=True, exist_ok=True)
            (args.dump / f'{name}.bin').write_bytes(raw)
        value = [struct.unpack_from('>I', raw, i)[0] for i in range(0, size - size % 4, 4)] \
            if args.words else raw.hex()
        result[name] = dict(address=address, size=size, value=value)
    if args.json:
        print(json.dumps(dict(state=str(state), order=memory.order, symbols=result), indent=2))
        return
    print(f'{state} (Work RAM {memory.order})')
    if args.cpus:
        for cpu, (pc, pr, sp) in cpus(state).items():
            print(f'{cpu} PC {pc:#010x} {function_at(table, pc)}  PR {pr:#010x} {function_at(table, pr)}  SP {sp:#010x}')
    for name, r in result.items():
        print(f"{name} {r['address']:#010x} {r['size']}: {r['value']}")


if __name__ == '__main__':
    main()
