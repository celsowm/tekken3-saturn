#!/usr/bin/env python3
"""Estimate the VDP1 drawing an image asks for, from its real command list.

Dumps VDP1 VRAM from Ymir at chosen fields and walks the command table. VDP1
draws a quad as lines between its left and right edges, so pixels written are
lines times line length, including overdraw. The default costs are Mednafen
1.32.1's model (vdp1_common.h, vdp1.cpp); the others show the margin. This is
an estimate, not a hardware measurement.

`bands` lists, for each BAND_LINES lines of the screen, the columns anything but
the clearing polygon touches, so a smaller clear can be estimated. A frame
buffer holds the image from two swaps back, so a clear must cover that too:
`--pairs` adds, for each two fields in turn, the area covering both.
"""
import argparse
import json
import struct
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VDP1_VRAM = 0x25C00000
VRAM_BYTES = 0x80000
MASTER_HZ = 26_846_591  # 320-wide NTSC clock
TEXTURED_SIZE = {0: 'normal', 1: 'scaled', 2: 'distorted'}
BAND_LINES = 16
SCREEN = 320 * 224


def signed(value):
    return value - 0x10000 if value & 0x8000 else value


def span(ax, ay, bx, by):
    return max(abs(ax - bx), abs(ay - by)) + 1


def clip_visible(xs, ys, width, height):
    return not (max(xs) < 0 or max(ys) < 0 or min(xs) >= width or min(ys) >= height)


def touch_bands(bands, xs, ys, width, height):
    x0, x1 = max(min(xs), 0), min(max(xs), width - 1)
    y0, y1 = max(min(ys), 0), min(max(ys), height - 1)
    for band in range(y0 // BAND_LINES, y1 // BAND_LINES + 1):
        low, high = bands[band]
        bands[band] = [min(low, x0), max(high, x1)]


def bands_pixels(bands):
    return sum((high - low + 1) * BAND_LINES for low, high in bands if high >= low)


def bands_union(a, b):
    return [[min(p[0], q[0]), max(p[1], q[1])] for p, q in zip(a, b)]


def walk(vram, width, height):
    stats = dict(commands=0, drawn=0, culled=0, lines=0, pixels=0, half_pixels=0, gouraud_pixels=0,
                 by_kind={}, biggest=[], local=[], clip=[],
                 bands=[[width, -1] for _ in range((height + BAND_LINES - 1) // BAND_LINES)])
    address, seen, ox, oy, stack = 0, set(), 0, 0, None
    while address + 32 <= len(vram) and address not in seen:
        seen.add(address)
        w = struct.unpack_from('>16H', vram, address)
        ctrl, link, pmod, xy = w[0], w[1], w[2], w[6:14]
        if ctrl & 0x8000:
            break
        jump, comm = (ctrl >> 12) & 7, ctrl & 15
        skipped = bool(ctrl & 0x4000)
        stats['commands'] += not skipped
        if not skipped and comm == 10:
            ox, oy = signed(w[6]), signed(w[7])
            stats['local'].append([ox, oy])
        if not skipped and comm == 9:
            stats['clip'].append([signed(w[10]), signed(w[11])])
        if not skipped and comm in (0, 1, 2, 4, 5, 6):
            xa, ya, xb, yb, xc, yc, xd, yd = (signed(v) for v in xy)
            xa, xb, xc, xd = xa + ox, xb + ox, xc + ox, xd + ox
            ya, yb, yc, yd = ya + oy, yb + oy, yc + oy, yd + oy
            if comm == 0:
                sw, sh = ((w[5] >> 8) & 0x3f) * 8, w[5] & 0xff
                xs, ys = [xa, xa + sw - 1], [ya, ya + sh - 1]
                lines, length, kind = sh, sw, 'normal'
            elif comm == 1:
                xs, ys = [xa, xc], [ya, yc]
                lines, length, kind = abs(yc - ya) + 1, abs(xc - xa) + 1, 'scaled'
            elif comm in (2, 4):
                xs, ys = [xa, xb, xc, xd], [ya, yb, yc, yd]
                lines = max(span(xa, ya, xd, yd), span(xb, yb, xc, yc))
                length = (span(xa, ya, xb, yb) + span(xd, yd, xc, yc) + 1) // 2
                kind = 'distorted' if comm == 2 else 'polygon'
            elif comm == 5:
                xs, ys = [xa, xb, xc, xd], [ya, yb, yc, yd]
                lines, length, kind = 4, (span(xa, ya, xb, yb) + span(xb, yb, xc, yc)
                                          + span(xc, yc, xd, yd) + span(xd, yd, xa, ya)) // 4, 'polyline'
            else:
                xs, ys = [xa, xb], [ya, yb]
                lines, length, kind = 1, span(xa, ya, xb, yb), 'line'
            if clip_visible(xs, ys, width, height):
                pixels = lines * length
                stats['drawn'] += 1
                stats['lines'] += lines
                stats['pixels'] += pixels
                mode = pmod & 7
                if mode in (1, 2, 3):
                    stats['half_pixels'] += pixels
                if mode >= 4:
                    stats['gouraud_pixels'] += pixels
                stats['by_kind'][kind] = stats['by_kind'].get(kind, 0) + pixels
                stats['biggest'].append([pixels, kind, address, pmod, min(xs), min(ys), max(xs), max(ys)])
                if pixels != SCREEN:
                    touch_bands(stats['bands'], xs, ys, width, height)
            else:
                stats['culled'] += 1
        if jump & 3 == 1:
            address = link * 8
        elif jump & 3 == 2:
            stack = address + 32
            address = link * 8
        elif jump & 3 == 3 and stack is not None:
            address, stack = stack, None
        else:
            address += 32
    stats['biggest'] = sorted(stats['biggest'], reverse=True)[:5]
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fields', type=int, nargs='+', required=True)
    parser.add_argument('--cost', type=float, nargs='+', default=[1.1875, 4, 6], help='assumed VDP1 clocks per pixel')
    parser.add_argument('--pairs', action='store_true', help='also estimate a clear covering each two fields in turn')
    parser.add_argument('--line-cost', type=float, default=4, help='assumed clocks per line')
    parser.add_argument('--command-cost', type=float, default=16, help='assumed clocks per command')
    parser.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    parser.add_argument('--bios', type=Path,
                        default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    devices = args.out / 'none.devices'
    devices.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n')

    def measure(field):
        dump = args.out / f'vram{field}.bin'
        shot = args.out / f'field{field}.png'
        subprocess.run([str(args.probe), '--iso', str(args.build / 'disc/music.cue'),
                        '--bin', str(args.build / 'tekken3_saturn.app.bin'), '--bios', str(args.bios),
                        '--frames', str(field), '--device-script', str(devices),
                        '--dump-mem', f'{VDP1_VRAM:#x}:{VRAM_BYTES}:{dump}',
                        '--screenshot', f'{field}:{shot}', '--out', str(args.out / f'probe{field}.json')],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        stats = walk(dump.read_bytes(), 320, 224)
        dump.unlink()
        return field, stats

    with ThreadPoolExecutor(max_workers=6) as pool:
        results = dict(pool.map(measure, args.fields))
    (args.out / 'vdp1-load.json').write_text(json.dumps(results, indent=1) + '\n')
    field_clocks = MASTER_HZ / 60
    for field, s in results.items():
        cost = [(s['pixels'] * c + s['lines'] * args.line_cost + s['commands'] * args.command_cost) / field_clocks
                for c in args.cost]
        print(f"field {field}: commands {s['commands']} drawn {s['drawn']} culled {s['culled']} lines {s['lines']} "
              f"pixels {s['pixels']} (half {s['half_pixels']}, gouraud {s['gouraud_pixels']}) {s['by_kind']}")
        print('   fields of VDP1 at ' + ', '.join(f'{c:g} clk/px = {x:.2f}' for c, x in zip(args.cost, cost)))
        print(f"   local {s['local'][:3]} clip {s['clip'][:2]} biggest {s['biggest'][:3]}")
        print(f"   area touched by {BAND_LINES}-line bands: {bands_pixels(s['bands'])} of {SCREEN}")
    if args.pairs:
        for first, second in zip(args.fields, args.fields[1:]):
            both = bands_pixels(bands_union(results[first]['bands'], results[second]['bands']))
            print(f"fields {first}+{second}: clear {both} of {SCREEN} ({both * 100 // SCREEN}%)")


if __name__ == '__main__':
    main()
