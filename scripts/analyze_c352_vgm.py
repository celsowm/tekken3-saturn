#!/usr/bin/env python3
"""Measure a Namco C352 VGM: voices, samples, ROM bytes, loops, pitch, polyphony.

Register layout and key-on semantics follow MAME's c352.cpp (BSD-3-Clause): eight 16-bit registers per
voice (vol_f, vol_r, freq, flags, wave_bank, wave_start, wave_end, wave_loop), key-on executes on a write
to 0x202. The output rate is clock / (4 * divider byte at 0xD6) = 88200 Hz here; VGM waits count 44100 Hz."""
import argparse
import collections
import gzip
import json
import struct
import sys

KEYON, KEYOFF, MULAW, LOOP, REVERSE, LINK, NOISE, BUSY = 0x4000, 0x2000, 0x0008, 0x0002, 0x0001, 0x0020, 0x0010, 0x8000


def read_vgm(path):
    raw = open(path, 'rb').read()
    return gzip.decompress(raw) if raw[:2] == b'\x1f\x8b' else raw


def parse(data):
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    if data[:4] != b'Vgm ':
        raise ValueError('not a VGM')
    clock = u32(0xDC) & 0x7FFFFFFF
    divider = data[0xD6] * 4
    out_rate = clock / divider
    start = 0x34 + u32(0x34)
    rom_size, rom = 0, {}
    events, t, p = [], 0, start          # events: (time in 44.1 kHz samples, register, value)
    while p < len(data):
        c = data[p]
        if c == 0x66:
            break
        if c == 0x67:
            t_, size = data[p + 2], u32(p + 3)
            if t_ == 0x92:
                total, addr = struct.unpack_from('<II', data, p + 7)
                rom_size = total
                rom[addr] = data[p + 15:p + 7 + size]
            p += 7 + size
        elif c == 0xE1:
            events.append((t, struct.unpack_from('>H', data, p + 1)[0], struct.unpack_from('>H', data, p + 3)[0]))
            p += 5
        elif c == 0x61:
            t += struct.unpack_from('<H', data, p + 1)[0]; p += 3
        elif c == 0x62:
            t += 735; p += 1
        elif c == 0x63:
            t += 882; p += 1
        elif 0x70 <= c <= 0x7F:
            t += (c & 15) + 1; p += 1
        elif c == 0x68:
            p += 12
        elif c in (0x4F, 0x50) or 0x30 <= c <= 0x3F:
            p += 2
        elif 0x40 <= c <= 0x4E or 0x51 <= c <= 0x5F or 0xA0 <= c <= 0xBF:
            p += 3
        elif 0xC0 <= c <= 0xDF:
            p += 4
        elif 0xE0 <= c <= 0xFF:
            p += 5
        else:
            raise ValueError(f'unknown command {c:#x} at {p:#x}')
    # 0x18 is the total sample count and 0x20 the loop length; 0x1C is a byte offset, not a time.
    loop_samples = u32(0x20)
    loop_start = u32(0x18) - loop_samples if loop_samples else t
    return dict(clock=clock, out_rate=out_rate, total=t, loop_start=loop_start, loop_samples=loop_samples,
                events=events, rom=rom, rom_size=rom_size)


def analyze(info):
    regs = [[0] * 8 for _ in range(32)]
    active = {}                             # voice -> note dict
    notes, poly_max, poly_t = [], 0, 0
    rate = info['out_rate']

    def close(v, t):
        n = active.pop(v)
        n['end'] = t / 44100
        notes.append(n)

    def sweep(t):
        """Close one-shot notes whose sample has run out by time t (seconds)."""
        for v in list(active):
            n = active[v]
            if n['flags'] & LOOP:
                continue
            length = ((n['wave_end'] - n['wave_start']) & 0xFFFF) + 1
            if n['rate_hz'] <= 0:
                continue
            done = n['consumed'] + n['rate_hz'] * (t - n['mark'])
            if done >= length:
                n['end'] = n['mark'] + (length - n['consumed']) / n['rate_hz']
                notes.append(active.pop(v))

    for t, reg, val in info['events']:
        sweep(t / 44100)
        if reg < 0x100 and reg % 8 == 2 and reg // 8 in active:
            n = active[reg // 8]
            now = t / 44100
            n['consumed'] += n['rate_hz'] * (now - n['mark'])
            n['mark'] = now
            n['rate_hz'] = rate * val / 65536
            n['freq'] = val
        if reg < 0x100:
            regs[reg // 8][reg % 8] = val
            if reg % 8 < 2 and reg // 8 in active and (regs[reg // 8][0] or regs[reg // 8][1]):
                active[reg // 8]['audible'] = True
        elif reg == 0x202:
            for v in range(32):
                r = regs[v]
                if r[3] & KEYON:
                    r[3] &= ~KEYON
                    if v in active:
                        close(v, t)
                    active[v] = dict(voice=v, start=t / 44100, bank=r[4] & 0xFF, wave_start=r[5], wave_end=r[6],
                                     wave_loop=r[7], flags=r[3] & 0xFF, freq=r[2], vol_f=r[0], vol_r=r[1],
                                     rate_hz=rate * r[2] / 65536, audible=bool(r[0] or r[1]), consumed=0.0, mark=t / 44100)
                    poly = len(active)
                    if poly > poly_max:
                        poly_max, poly_t = poly, t / 44100
                if r[3] & KEYOFF:
                    r[3] &= ~KEYOFF
                    if v in active:
                        close(v, t)
    sweep(info['total'] / 44100)
    for v in list(active):
        close(v, info['total'])
    samples = {}
    for n in notes:
        key = (n['bank'], n['wave_start'], n['wave_end'], n['wave_loop'], n['flags'] & (MULAW | LOOP | REVERSE | LINK))
        # pos is 24 bits but only its low 16 bits are compared with wave_end, so a sample may run into the next bank.
        length = ((key[2] - key[1]) & 0xFFFF) + 1
        s = samples.setdefault(key, dict(bank=key[0], start=key[1], end=key[2], loop=key[3] if key[4] & LOOP else None,
                                         flags=key[4], uses=0, length=length, rom_address=key[0] << 16 | key[1]))
        s['uses'] += 1
    return notes, samples, poly_max, poly_t


def report(path):
    info = parse(read_vgm(path))
    notes, samples, poly_max, poly_t = analyze(info)
    spans = sorted((s['rom_address'], s['rom_address'] + s['length'] - 1) for s in samples.values())
    union, last = 0, -1
    for a, b in spans:
        a = max(a, last + 1)
        if b >= a:
            union += b - a + 1
            last = b
    mulaw = sum(1 for s in samples.values() if s['flags'] & MULAW)
    looped = sum(1 for s in samples.values() if s['flags'] & LOOP)
    rates = [n['rate_hz'] for n in notes]
    return dict(seconds=info['total'] / 44100, output_rate_hz=info['out_rate'], register_writes=len(info['events']),
                rom_blocks=len(info['rom']), rom_block_bytes=sum(len(b) for b in info['rom'].values()),
                notes=len(notes), unique_samples=len(samples), referenced_rom_bytes_union=union,
                referenced_rom_bytes_sum=sum(s['length'] for s in samples.values()),
                mulaw_samples=mulaw, looped_samples=looped, max_polyphony=poly_max, max_polyphony_at_s=poly_t,
                voices_used=len({n['voice'] for n in notes}), pitch_hz_min=min(rates), pitch_hz_max=max(rates),
                events_per_second=len(info['events']) / (info['total'] / 44100),
                keyons_per_second=len(notes) / (info['total'] / 44100),
                loop_start_s=info['loop_start'] / 44100,
                noise_notes=sum(1 for n in notes if n['flags'] & NOISE)), notes, samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vgm')
    parser.add_argument('--samples', action='store_true', help='also list every unique sample')
    parser.add_argument('--silent', action='store_true', help='list the longest notes that never have volume')
    args = parser.parse_args()
    summary, notes, samples = report(args.vgm)
    if args.samples:
        summary['samples'] = sorted(samples.values(), key=lambda s: -s['uses'])
    if args.silent:
        silent = [n for n in notes if not n['audible']]
        summary['silent_notes'] = len(silent)
        summary['silent_longest'] = [dict(voice=n['voice'], bank=n['bank'], seconds=round(n['end'] - n['start'], 1),
                                          length=((n['wave_end'] - n['wave_start']) & 0xFFFF) + 1)
                                     for n in sorted(silent, key=lambda n: n['start'] - n['end'])[:8]]
    json.dump(summary, sys.stdout, indent=2)
    print()


if __name__ == '__main__':
    main()
