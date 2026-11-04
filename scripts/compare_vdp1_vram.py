#!/usr/bin/env python3
"""Compare staged and banked VDP1 VRAM captures of the same scene.

The inputs are 512 KiB big-endian dumps at 0x05c00000. This compares the
contiguous command tables, referenced texture/LUT/Gouraud data and END. It
reports the first difference; it does not emulate command flow or rendering.
"""
import argparse
import struct
from pathlib import Path

VRAM = 512 * 1024
BANK = 80 * 1024
ROOT = 32
COMMANDS = 64 * 1024
FIELDS = 'ctrl link pmod colr srca size xa ya xb yb xc yc xd yd grda pad'.split()


def load(path, candidate):
    buf = Path(path).read_bytes()
    if len(buf) != VRAM:
        raise ValueError(f'{path}: expected {VRAM} bytes, got {len(buf)}')
    base = 0
    if candidate:
        ctrl, link = struct.unpack_from('>HH', buf)
        base = link * 8
        if ctrl != 0x5000 or base not in (ROOT, ROOT + BANK):
            raise ValueError(f'{path}: invalid root {ctrl:#x}/{link:#x}')
    return buf, base, ROOT + 2 * BANK if candidate else BANK


def region(buf, addr, size, lower, upper, label):
    if addr < lower or addr + size > upper:
        raise ValueError(f'{label} outside permitted VRAM: {addr:#x}+{size:#x}')
    return buf[addr:addr + size]


def record(buf, base, tex, index):
    words = list(struct.unpack_from('>16H', buf, base + 32 * index))
    ctrl, link, pmod, colr, srca, size = words[:6]
    if ctrl & 0x8000:
        return ('END',), ()
    kind = ctrl & 0xf
    # Only assign/call forms use CMDLINK; the other encodings ignore it.
    if ((ctrl >> 12) & 3) in (1, 2):
        target = link * 8
        if target < base or target >= base + COMMANDS or (target - base) % 32:
            raise ValueError(f'command {index}: invalid CMDLINK {link:#x}')
        words[1] = (target - base) // 8
    else:
        words[1] = 0
    data = []
    if kind in (0, 1, 2, 3):
        mode = (pmod >> 3) & 7
        width, height = ((size >> 8) & 63) * 8, size & 255
        if mode not in (0, 1, 2, 3, 4, 5) or not width or not height:
            raise ValueError(f'command {index}: unsupported texture mode/size')
        length = width * height * (2 if mode == 5 else 1)
        if mode in (0, 1):
            length //= 2
        address = srca * 8
        data.append(('texture', region(buf, address, length, tex, VRAM, f'command {index} texture')))
        words[4] = (address - tex) // 8
        if mode == 1:
            address = colr * 8
            data.append(('LUT', region(buf, address, 32, tex, VRAM, f'command {index} LUT')))
            words[3] = (address - tex) // 8
    else:
        words[4] = 0  # unused source address
    if kind in (0, 1, 2, 3, 4, 5, 6) and (pmod & 7) in (4, 5, 6, 7):
        address = words[14] * 8
        data.append(('Gouraud', region(buf, address, 8, base + COMMANDS, base + BANK,
                                       f'command {index} Gouraud')))
        words[14] = (address - base - COMMANDS) // 8
    else:
        words[14] = 0
    words[15] = 0  # reserved
    return tuple(words), tuple(data)


def compare(control, candidate):
    a, ab, at = load(control, False)
    b, bb, bt = load(candidate, True)
    for i in range(2048):
        ac, ad = record(a, ab, at, i)
        bc, bd = record(b, bb, bt, i)
        if ac != bc:
            if len(ac) == 1 or len(bc) == 1:
                raise ValueError(f'command {i}: END differs: {ac} vs {bc}')
            field = next(j for j in range(16) if ac[j] != bc[j])
            raise ValueError(f'command {i} {FIELDS[field]}: {ac[field]:#x} vs {bc[field]:#x}')
        for (name, left), (_, right) in zip(ad, bd):
            if left != right:
                pos = next(j for j, (x, y) in enumerate(zip(left, right)) if x != y)
                raise ValueError(f'command {i} {name}: first differing byte {pos:#x}')
        if len(ad) != len(bd):
            raise ValueError(f'command {i}: referenced data count differs')
        if ac == ('END',):
            print(f'MATCH: {i} commands plus END; candidate bank {bb:#x}')
            return
    raise ValueError('no END within 2048 entries')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('control')
    parser.add_argument('candidate')
    args = parser.parse_args()
    try:
        compare(args.control, args.candidate)
    except ValueError as error:
        parser.exit(1, f'{error}\n')
