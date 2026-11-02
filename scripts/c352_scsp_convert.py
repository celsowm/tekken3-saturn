#!/usr/bin/env python3
"""Convert a window of a Namco C352 VGM into SCSP data: a sample bank and a timed register score.

C352 side (MAME c352.cpp / libvgm c352.c, BSD-3-Clause): 32 voices, eight 16-bit registers each (vol_f, vol_r,
freq, flags, bank, start, end, loop); key-on and key-off run on a write to 0x202; a voice steps through its
8-bit linear or mu-law ROM sample at 88200 * freq / 65536 Hz; front and rear volumes (8 bits per side) are both
summed into the stereo output. SCSP side (Sega SCSP manual, Ymir scsp.cpp / scsp_slot.hpp): 44100 Hz output,
slot registers 0x20 bytes apart, pitch OCT/FNS, level TL (0.375 dB), DISDL (6 dB), DIPAN (3 dB, bit 4 attenuates
the right side), loop points LSA/LEA in samples, key-on only from the Release state.

Outputs (all private, under --out):
  bank.bin       PCM8 area then PCM16 area, big-endian, placed at --pcm8-base / --pcm16-base in Sound RAM
  score.bin      8-byte big-endian events (tick u32, register u16, value u16) for sat_sound_driver_score_load
  manifest.json  window, samples, slots, event counts, quantisation and the settings used
No equivalence between the chips is assumed: interpolation, envelope and loop behaviour differ, and the
conversion reports what it could not carry (phase invert, reverse play, volume ramp, 16-bit loop limits)."""
import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import analyze_c352_vgm as vgm

TICK_SAMPLES = 256                          # SCSP timer A period: one driver tick (256, 128 or 64 samples)
TICK_SECONDS = TICK_SAMPLES / 44100
T0 = 0.0                                    # window start (s): stream time 0


def set_timebase(tick_samples=256, t0=0.0):
    global TICK_SAMPLES, TICK_SECONDS, T0
    TICK_SAMPLES, TICK_SECONDS, T0 = tick_samples, tick_samples / 44100, t0
KYONEX, KYONB = 0x1000, 0x0800
PCM8B, LPCTL_SHIFT = 0x0010, 5
SCORE_MAX_EVENTS = 1024
SLOTS = 32
STATIC_SLOT_REGS = {0x08: 0x001F, 0x0A: 0x3C1F, 0x0E: 0x0000, 0x12: 0x0000, 0x14: 0x0000}   # AR 31, RR 31, no LFO/DSP


def mulaw_table():
    """libvgm's C352 mu-law expansion: 16-bit values from the 8-bit code."""
    table, j = [0] * 256, 0
    for i in range(128):
        table[i] = (j << 5) & 0xFFFF
        j += 1 if i < 16 else 2 if i < 24 else 4 if i < 48 else 8 if i < 100 else 16
    for i in range(128, 256):
        table[i] = (~table[i - 128]) & ~0x1F & 0xFFFF
    return [v - 65536 if v >= 32768 else v for v in table]


def rom_image(info):
    rom = bytearray(b'\xff' * info['rom_size'])
    for address, block in info['rom'].items():
        rom[address:address + len(block)] = block
    return rom


class Note:
    def __init__(self, voice, t, regs):
        self.voice, self.t_on = voice, t
        self.vol_f, self.vol_r, self.freq, self.flags = regs[0], regs[1], regs[2], regs[3]
        self.bank, self.start, self.end, self.loop = regs[4] & 0xFF, regs[5], regs[6], regs[7]
        self.changes = []                   # (time s, 'vol'|'freq', vol_f, vol_r, freq)
        self.t_off = None                   # key-off or retrigger (s)
        self.t_natural = None               # one-shot sample runs out (s)
        self.looped = bool(self.flags & vgm.LOOP)
        self.length = ((self.end - self.start) & 0xFFFF) + 1
        self.key = (self.bank, self.start, self.end, bool(self.flags & vgm.MULAW))

    def volumes(self):
        return self.vol_f, self.vol_r, self.freq


def run_out(n, t_end, out_rate):
    """(time the sample runs out or None, samples consumed by t_end) for a note, following its pitch changes."""
    consumed, mark, freq = 0.0, n.t_on, n.changes_freq0
    for t_c, kind, vf, vr, fr in [c for c in n.changes if c[0] < t_end] + [(t_end, 'end', 0, 0, freq)]:
        if kind == 'vol':
            continue
        rate = out_rate * freq / 65536
        if rate > 0 and not n.looped and consumed + rate * (t_c - mark) >= n.length:
            return mark + (n.length - consumed) / rate, n.length
        consumed += rate * (t_c - mark)
        mark = t_c
        if kind == 'freq':
            freq = fr
    return None, consumed


def timeline(info, t0, t1):
    """Notes that start in [t0, t1) with their parameter changes, ended by key-off, run-out or t1."""
    out_rate = info['out_rate']
    regs = [[0] * 8 for _ in range(32)]
    active, notes = {}, []

    def close(n, t):
        if n.t_off is None:
            n.t_off = t

    def natural_end(n, t_end):
        """Time the one-shot sample runs out, integrating freq changes up to t_end; None if it does not by then."""
        return run_out(n, t_end, out_rate)

    def sweep(t):
        for v in list(active):
            n = active[v]
            if n.looped:
                continue
            end = natural_end(n, t)[0]
            if end is not None:
                n.t_natural = end
                active.pop(v)

    for tick, reg, val in info['events']:
        t = tick / 44100
        if t >= t1:
            break
        sweep(t)
        if reg < 0x100:
            v, f = reg // 8, reg % 8
            regs[v][f] = val
            if v in active and f in (0, 1, 2):
                n = active[v]
                kind = 'freq' if f == 2 else 'vol'
                n.changes.append((t, kind, regs[v][0], regs[v][1], regs[v][2]))
        elif reg == 0x202:
            for v in range(32):
                r = regs[v]
                if r[3] & vgm.KEYON:
                    r[3] &= ~vgm.KEYON
                    if v in active:
                        close(active.pop(v), t)
                    n = Note(v, t, r)
                    n.changes_freq0 = r[2]
                    active[v] = n
                    notes.append(n)
                elif r[3] & vgm.KEYOFF:
                    r[3] &= ~vgm.KEYOFF
                    if v in active:
                        close(active.pop(v), t)
    sweep(t1)
    for n in active.values():
        close(n, t1)
    for n in notes:
        if n.t_natural is None and n.t_off is None:
            n.t_off = t1
    kept = []
    for n in notes:
        end = min(x for x in (n.t_off, n.t_natural) if x is not None)
        n.carried = n.t_on < t0
        if n.t_on >= t0:
            kept.append(n)
        elif end > t0:
            # sounding at the window start: it enters at t0 at the sample position the C352 had reached
            n.consumed0 = run_out(n, t0, out_rate)[1]
            n.t_on_original, n.t_on = n.t_on, t0
            kept.append(n)
    return kept


def gains(vol_f, vol_r):
    return ((vol_f >> 8) + (vol_r >> 8)) / 256, ((vol_f & 0xFF) + (vol_r & 0xFF)) / 256


def pan_attenuation(k):
    """Ymir's DIPAN step k (0..14): one shift per two steps, a quarter off for odd steps; 15 is silence."""
    return 0.0 if k >= 15 else (0.5 ** (k >> 1)) * (0.75 if k & 1 else 1.0)


def level_pan(vol_f, vol_r, scale):
    """(TL, DISDL/DIPAN word) for a C352 front/rear volume pair; TL=0xFF and DISDL 0 for silence."""
    gl, gr = (g * scale for g in gains(vol_f, vol_r))
    loud = max(gl, gr)
    if loud <= 0:
        return 0xFF, 0x0000
    tl = min(0xFF, max(0, round(-20 * math.log10(min(loud, 1.0)) / 0.375)))
    ratio = min(gl, gr) / loud
    if ratio <= 0:
        best = 15
    else:
        best = min(range(15), key=lambda k: abs(math.log(pan_attenuation(k) if k else 1.0) - math.log(ratio)))
        if ratio < pan_attenuation(14) / 2:
            best = 15
    side = 0x10 if gl > gr else 0           # bit 4 set: the right side is the attenuated one
    return tl, (7 << 13) | ((side | best) << 8)


def pitch_word(rate_hz):
    ratio = rate_hz / 44100
    octave = max(-8, min(7, math.floor(math.log2(ratio))))
    fns = round((ratio / 2 ** octave - 1) * 1024)
    if fns > 1023:
        octave, fns = (octave + 1, 0) if octave < 7 else (7, 1023)
    return ((octave & 0xF) << 11) | fns


def tick_of(t):
    """Driver tick of a VGM time, counted from the window start."""
    return round((t - T0) / TICK_SECONDS)


def plan(notes, rate_hz_base, scale, t1):
    """Slot allocation and the per-tick register writes. Returns (events, slot_of_note, stats)."""
    slots = [dict(free_after=-1, cfg=None, tl=None, pan=None, pitch=None, off_pending=False, note=None) for _ in range(SLOTS)]
    by_tick = {}

    def add(tick, order, reg, val):
        by_tick.setdefault(tick, []).append((order, reg, val))

    dropped, reused_same, reuse_other = [], 0, 0
    placement = []
    for n in sorted(notes, key=lambda n: n.t_on):
        n.on_tick = tick_of(n.t_on)
        end = n.t_off if n.t_off is not None else n.t_natural
        n.keyed_off = n.t_off is not None and (n.looped or n.t_natural is None or n.t_off < n.t_natural)
        n.off_tick = tick_of(n.t_off) if n.keyed_off else None
        n.end_tick = tick_of(end) if end is not None else n.on_tick
        if n.off_tick is not None and n.off_tick <= n.on_tick:
            n.off_tick = n.on_tick + 1              # a note shorter than a tick still gets one tick
            n.end_tick = max(n.end_tick, n.off_tick)

    for n in sorted(notes, key=lambda n: n.t_on):
        cfg = n.cfg
        free = [i for i, s in enumerate(slots) if s['free_after'] < n.on_tick]
        if not free:
            dropped.append(n)
            continue
        same = [i for i in free if slots[i]['cfg'] == cfg]
        i = same[0] if same else min(free, key=lambda i: slots[i]['free_after'])
        s = slots[i]
        n.slot = i
        base = i * 0x20
        if s['off_pending']:
            by_tick_off = n.on_tick - 1
            add(by_tick_off, 1, base + 0x00, s['cfg_word'])      # KYONB=0, ahead of the key-on
            s['off_pending'] = False
        if same:
            reused_same += 1
        else:
            reuse_other += s['cfg'] is not None
            add(n.on_tick, 3, base + 0x00, cfg[0])
            add(n.on_tick, 3, base + 0x02, cfg[1])
            add(n.on_tick, 3, base + 0x04, cfg[2])
            add(n.on_tick, 3, base + 0x06, cfg[3])
        vf, vr, fr = n.vol_f, n.vol_r, n.freq
        for t_c, kind, cvf, cvr, cfr in n.changes:
            if tick_of(t_c) <= n.on_tick:
                vf, vr, fr = cvf, cvr, cfr
        tl, pan = level_pan(vf, vr, scale)
        pitch = pitch_word(rate_hz_base * fr / 65536) if fr else 0
        if s['tl'] != tl or not same:
            add(n.on_tick, 3, base + 0x0C, tl)
        if s['pan'] != pan or not same:
            add(n.on_tick, 3, base + 0x16, pan)
        if s['pitch'] != pitch or not same:
            add(n.on_tick, 3, base + 0x10, pitch)
        s.update(cfg=cfg, cfg_word=cfg[0], tl=tl, pan=pan, pitch=pitch, note=n)
        add(n.on_tick, 5, base + 0x00, cfg[0] | KYONB)
        # parameter changes while sounding (last write per tick and register wins)
        last = {}
        for t_c, kind, vf, vr, fr in n.changes:
            tk = tick_of(t_c)
            if tk <= n.on_tick:
                continue                            # already folded into the initial values
            if n.off_tick is not None and tk >= n.off_tick:
                continue
            if tk > n.end_tick:
                continue
            if kind == 'vol':
                last[(tk, 'vol')] = level_pan(vf, vr, scale)
            else:
                last[(tk, 'freq')] = pitch_word(rate_hz_base * fr / 65536) if fr else 0
        cur_tl, cur_pan, cur_pitch = tl, pan, pitch
        for (tk, kind), val in sorted(last.items()):
            if kind == 'vol':
                if val[0] != cur_tl:
                    add(tk, 2, base + 0x0C, val[0]); cur_tl = val[0]
                if val[1] != cur_pan:
                    add(tk, 2, base + 0x16, val[1]); cur_pan = val[1]
            elif val != cur_pitch:
                add(tk, 2, base + 0x10, val); cur_pitch = val
        s['tl'], s['pan'], s['pitch'] = cur_tl, cur_pan, cur_pitch
        if n.keyed_off:
            add(n.off_tick, 4, base + 0x00, cfg[0])
            s['free_after'] = n.off_tick
            s['off_pending'] = False
        else:
            s['free_after'] = n.end_tick + 1        # one tick of margin: the sample may run a hair long
            s['off_pending'] = True
        placement.append(n)
    # key writes carry KYONEX once per tick, on the last key write
    events = []
    for tick in sorted(by_tick):
        writes = sorted(by_tick[tick], key=lambda w: w[0])
        key_idx = [k for k, w in enumerate(writes) if w[2] is not None and w[1] % 0x20 == 0 and w[0] in (1, 4, 5)]
        out = [list(w) for w in writes]
        if key_idx:
            out[key_idx[-1]][2] |= KYONEX
        events.extend((tick, w[1], w[2]) for w in out)
    return events, placement, dropped, dict(reused_same_config=reused_same, reconfigured=reuse_other)


def build_bank(notes, rom, pcm8_base, align=2):
    """PCM8 then PCM16 areas; returns (bytes, sample table, key -> (address, is16))."""
    table = mulaw_table()
    keys = {}
    for n in notes:
        keys.setdefault(n.key, n.length)
    pcm8, pcm16, where = bytearray(), bytearray(), {}
    for key in sorted(keys):
        bank, start, end, mu = key
        length = keys[key]
        base = (bank << 16) | start
        chunk = bytes(rom[(base + i) % len(rom)] for i in range(length))
        if mu:
            where[key] = ('pcm16', len(pcm16), length)
            pcm16 += b''.join(struct.pack('>h', table[b]) for b in chunk)
        else:
            where[key] = ('pcm8', len(pcm8), length)
            pcm8 += chunk
            pcm8 += b'\0' * (-len(pcm8) % align)
        pcm16 += b'\0' * (-len(pcm16) % align)
    pcm16_base = pcm8_base + len(pcm8)
    addresses = {k: (pcm8_base + off if kind == 'pcm8' else pcm16_base + off, kind == 'pcm16', ln)
                 for k, (kind, off, ln) in where.items()}
    return bytes(pcm8 + pcm16), addresses, len(pcm8), len(pcm16), pcm16_base


def end_of(n):
    return min(x for x in (n.t_off, n.t_natural) if x is not None)


def plan_dynamic_bank(notes, addresses, base, budget, lead_s=1.5, tail_s=0.1, merge_gap_s=3.0):
    """Residencies of the samples in Sound RAM over time, packed into [base, base + budget).

    A sample is resident from `lead_s` before its first use to `tail_s` after its last, uses closer than
    `merge_gap_s` sharing one residency; a longer silence frees the space and the sample is read again. Residencies
    are placed in time order at the lowest address where they overlap no earlier one in both space and time. Each
    carries the interval in which it may be written: after the last residency it overlaps in space has ended, and
    before its own first use. Returns (residencies, loads, report); notes get `address_override`."""
    by_key = {}
    for n in sorted(notes, key=lambda n: n.t_on):
        by_key.setdefault(n.key, []).append(n)
    res = []
    for key, ns in by_key.items():
        cur = None
        for n in ns:
            s0, e0 = n.t_on - T0, end_of(n) - T0
            if cur and s0 - cur['last'] < merge_gap_s:
                cur['last'] = max(cur['last'], e0)
                cur['notes'].append(n)
            else:
                cur = dict(key=key, first=s0, last=e0, notes=[n])
                res.append(cur)
    res.sort(key=lambda r: r['first'])
    placed = []
    for r in res:
        address, is16, length = addresses[r['key']]
        r['file_offset'] = address - base
        r['bytes'] = length * (2 if is16 else 1) + (length * (2 if is16 else 1)) % 2
        r['is16'], r['length'] = is16, length
        r['span'] = (max(0.0, r['first'] - lead_s) if r['first'] > lead_s else -1e9, r['last'] + tail_s)
        busy = sorted((p['addr'], p['addr'] + p['bytes']) for p in placed
                      if p['span'][1] > r['span'][0] and p['span'][0] < r['span'][1])
        cand = 0
        for a, b in busy:
            if cand + r['bytes'] <= a:
                break
            cand = max(cand, b)
        r['addr'] = cand
        placed.append(r)
    extent = max((r['addr'] + r['bytes'] for r in res), default=0)
    if extent > budget:
        raise ValueError(f'the section samples need {extent} bytes of Sound RAM at once; budget {budget}')
    loads = []
    for r in res:
        prev = [p['span'][1] for p in res if p is not r and p['span'][0] < r['span'][0] and
                p['addr'] < r['addr'] + r['bytes'] and r['addr'] < p['addr'] + p['bytes']]
        r['load_after'] = max(prev) if prev else -1e9
        for n in r['notes']:
            n.address_override = (base + r['addr'], r['is16'], r['length'])
        loads.append(r)
    loads.sort(key=lambda r: (r['load_after'], r['first']))
    events = sorted([(r['span'][0], r['bytes']) for r in res] + [(r['span'][1], -r['bytes']) for r in res])
    cur = peak = 0
    for _, d in sorted(events, key=lambda e: (e[0], e[1])):
        cur += d
        peak = max(peak, cur)
    report = dict(residencies=len(res), samples=len(by_key), reloaded_samples=len(res) - len(by_key),
                  bytes_loaded=sum(r['bytes'] for r in res), union_bytes=sum(
                      (addresses[k][2] * (2 if addresses[k][1] else 1)) for k in by_key),
                  peak_live_bytes=peak, packed_extent_bytes=extent, budget_bytes=budget,
                  lead_s=lead_s, tail_s=tail_s, merge_gap_s=merge_gap_s)
    return res, loads, report


def loads_bytes(loads, base, sample_file):
    """LOADS.BIN: per residency (load_after tick, needed_by tick, Sound RAM address, BANK.BIN offset, bytes, word sum)."""
    out = b''
    for r in loads:
        la = -1 if r['load_after'] < -1e8 else int(math.ceil(r['load_after'] / TICK_SECONDS))
        nb = int(r['first'] / TICK_SECONDS)
        data = sample_file[r['file_offset']:r['file_offset'] + r['bytes']]
        data += b'\0' * (-len(data) % 2)
        total = sum(struct.unpack(f'>{len(data) // 2}H', data)) & 0xFFFFFFFF
        out += struct.pack('>iIIIII', la, nb, base + r['addr'], r['file_offset'], len(data), total)
    return out


def configure(notes, addresses):
    """Per-note slot configuration words (ctrl, SA low, LSA, LEA) and unsupported-feature counters."""
    issues = dict(reverse_play=0, phase_invert=0, link=0, lea_clamped=0, noise=0)
    for n in notes:
        address, is16, length = getattr(n, 'address_override', None) or addresses[n.key]
        flags = n.flags
        lpctl = 0
        if flags & vgm.LOOP:
            lpctl = 3 if flags & vgm.REVERSE else 1
        elif flags & vgm.REVERSE:
            issues['reverse_play'] += 1
        issues['phase_invert'] += bool(flags & 0x0380)
        issues['link'] += bool(flags & vgm.LINK)
        issues['noise'] += bool(flags & vgm.NOISE)
        lsa = ((n.loop - n.start) & 0xFFFF) if lpctl else 0
        lea = length
        skip = int(getattr(n, 'consumed0', 0.0)) if getattr(n, 'carried', False) else 0
        if skip:
            # a note already sounding at the window start enters where the C352 had got to; inside a loop it
            # enters at the loop start (the SCSP loop points are relative to the entry address)
            if lpctl and skip >= lsa:
                skip = lsa
            skip = min(skip, max(0, length - 2))
            sa = address + skip * (2 if is16 else 1)
            lsa = max(0, lsa - skip)
            lea = length - skip
            n.cfg = (((sa >> 16) & 0xF) | (lpctl << LPCTL_SHIFT) | (0 if is16 else PCM8B), sa & 0xFFFF, lsa, min(lea, 0xFFFF))
            continue
        if lea > 0xFFFF:
            lea = 0xFFFF
            issues['lea_clamped'] += 1
        ctrl = ((address >> 16) & 0xF) | (lpctl << LPCTL_SHIFT) | (0 if is16 else PCM8B)
        n.cfg = (ctrl, address & 0xFFFF, lsa, lea)
    return issues


def audible(n):
    """False for notes that never get a non-zero volume."""
    if n.vol_f or n.vol_r:
        return True
    return any(c[1] == 'vol' and (c[2] or c[3]) for c in n.changes)


def convert(vgm_path, t0, t1, pcm8_base, scale=None, out=None, tick_samples=256, bank_budget=None):
    set_timebase(tick_samples, t0)
    info = vgm.parse(vgm.read_vgm(vgm_path))
    rom = rom_image(info)
    notes_all = timeline(info, t0, t1)
    silent = [n for n in notes_all if not audible(n)]
    notes = [n for n in notes_all if audible(n)]
    peak_gain = max((max(gains(n.vol_f, n.vol_r)) for n in notes), default=1.0)
    for n in notes:
        for c in n.changes:
            if c[1] == 'vol':
                peak_gain = max(peak_gain, max(gains(c[2], c[3])))
    if scale is None:
        scale = 1.0 / max(1.0, peak_gain)
    bank, addresses, pcm8_len, pcm16_len, pcm16_base = build_bank(notes, rom, pcm8_base)
    dynamic = plan_dynamic_bank(notes, addresses, pcm8_base, bank_budget) if bank_budget else None
    issues = configure(notes, addresses)
    events, placed, dropped, reuse = plan(notes, info['out_rate'], scale, t1)
    loop_ticks = tick_of(t1) + 8
    score = b''.join(struct.pack('>IHH', t, r, v) for t, r, v in events)
    poly = peak_polyphony(notes)
    return dict(info=info, notes=notes, silent=silent, bank=bank, addresses=addresses, score=score, events=events,
                dropped=dropped, reuse=reuse, issues=issues, scale=scale, peak_gain=peak_gain, loop_ticks=loop_ticks,
                pcm8_len=pcm8_len, pcm16_len=pcm16_len, pcm16_base=pcm16_base, poly=poly, placed=placed,
                notes_all=notes_all, dynamic=dynamic)


PAGE_EVENTS = 512
PAGE_HEADER = 16


def paginate(events, end_tick, relative=False):
    """Split sorted (tick, register, value) events into driver pages of at most 512, cutting between ticks when
    it can. Page time: absolute (time base 0) or relative to the page's first event."""
    pages, i = [], 0
    while i < len(events):
        chunk = events[i:i + PAGE_EVENTS]
        if i + PAGE_EVENTS < len(events) and chunk[-1][0] == events[i + PAGE_EVENTS][0]:
            k = len(chunk)
            while k > 1 and chunk[k - 1][0] == chunk[-1][0]:
                k -= 1
            if k > 1:
                chunk = chunk[:k]
        j = i + len(chunk)
        end = events[j][0] if j < len(events) else end_tick
        base = chunk[0][0] if relative else 0
        pages.append(dict(count=len(chunk), time_base=base, end=end - base, last=j >= len(events),
                          events=[(t - base, r, v) for t, r, v in chunk]))
        i = j
    return pages


def pages_bytes(pages):
    """pages.bin: per page a 16-byte header (count, flags bit 15 = last, time base, end, 0) then count x 8-byte events."""
    out = bytearray()
    for pg in pages:
        out += struct.pack('>HHIII', pg['count'], 0x8000 if pg['last'] else 0, pg['time_base'], pg['end'], 0)
        for t, r, v in pg['events']:
            out += struct.pack('>IHH', t, r, v)
    return bytes(out)


def peak_polyphony(notes):
    edges = []
    for n in notes:
        end = n.t_off if n.t_off is not None else n.t_natural
        if n.t_natural is not None and n.t_off is not None:
            end = min(n.t_off, n.t_natural)
        edges += [(n.t_on, 1), (end if end is not None else n.t_on, -1)]
    cur = peak = 0
    at = 0.0
    for t, d in sorted(edges, key=lambda e: (e[0], e[1])):
        cur += d
        if cur > peak:
            peak, at = cur, t
    return peak, at


def loop_seams(notes, rom):
    """For each looped sample: the step across the loop point against the typical step inside the loop (16-bit units)."""
    table, seen, rows = mulaw_table(), set(), []
    for n in notes:
        if not n.looped or (n.key, n.loop) in seen:
            continue
        seen.add((n.key, n.loop))
        base = (n.bank << 16) | n.start
        data = [table[b] if n.key[3] else (b - 256 if b > 127 else b) * 256
                for b in (rom[(base + i) % len(rom)] for i in range(n.length))]
        ls = (n.loop - n.start) & 0xFFFF
        body = [abs(data[i + 1] - data[i]) for i in range(ls, n.length - 1)]
        typical = sorted(body)[len(body) // 2] if body else 0
        rows.append(dict(bank=n.bank, start=n.start, loop=n.loop, end=n.end, mode='ping-pong' if n.flags & vgm.REVERSE else 'forward',
                         loop_samples=n.length - ls, seam_step=abs(data[ls] - data[n.length - 1]), typical_step=typical))
    return rows


def change_stats(notes, out_rate):
    """How many volume, pan and pitch changes the notes carry and the slowest C352 volume ramp (1 step per half sample)."""
    vol = pan = pitch = 0
    ramp = 0.0
    for n in notes:
        cur = (n.vol_f, n.vol_r)
        rate = out_rate * n.freq / 65536
        ramp = max(ramp, max(max(gains(*cur)) * 256, 1) / (2 * rate) * 1000 if rate else 0)
        for t, kind, vf, vr, fr in n.changes:
            if kind == 'freq':
                pitch += 1
                rate = out_rate * fr / 65536
            else:
                vol += (vf, vr) != cur
                pan += level_pan(vf, vr, 1.0)[1] != level_pan(*cur, 1.0)[1]
                step = max(abs((vf >> 8) - (cur[0] >> 8)), abs((vf & 255) - (cur[0] & 255)),
                           abs((vr >> 8) - (cur[1] >> 8)), abs((vr & 255) - (cur[1] & 255)))
                ramp = max(ramp, step / (2 * rate) * 1000 if rate else 0)
                cur = (vf, vr)
    return dict(volume_changes=vol, pan_changes=pan, pitch_changes=pitch, volume_ramp_ms_max=ramp)


def page_stats(r):
    per_tick = {}
    for t, _, _ in r['events']:
        per_tick[t] = per_tick.get(t, 0) + 1
    pages = paginate(r['events'], r['loop_ticks'])
    return dict(tick_samples=TICK_SAMPLES, events=len(r['events']), pages=len(pages), page_events=PAGE_EVENTS,
                ticks_with_events=len(per_tick), max_events_in_one_tick=max(per_tick.values(), default=0),
                mean_events_per_tick=len(r['events']) / max(1, r['loop_ticks']),
                window_ticks=r['loop_ticks'], pages_bytes=len(pages_bytes(pages)))


def summary(r, args, vgm_path):
    notes = r['notes']
    kinds = {}
    for t, reg, val in r['events']:
        field = reg % 0x20
        kinds[f'{field:#04x}'] = kinds.get(f'{field:#04x}', 0) + 1
    raw_changes = sum(len(n.changes) for n in notes)
    window_events = [e for e in r['info']['events'] if args.start <= e[0] / 44100 < args.end]
    quant = [abs(n.on_tick * TICK_SECONDS + T0 - n.t_on) * 1000 for n in r['placed']]
    pitch_err = []
    for n in r['placed']:
        if n.freq:
            rate = r['info']['out_rate'] * n.freq / 65536
            w = pitch_word(rate)
            got = 44100 * (1 + (w & 0x3FF) / 1024) * 2 ** (((w >> 11) & 0xF) - (16 if (w >> 11) & 8 else 0))
            pitch_err.append(1200 * math.log2(got / rate))
    sample_rows = []
    for key, (address, is16, length) in sorted(r['addresses'].items(), key=lambda kv: kv[1][0]):
        uses = sum(1 for n in notes if n.key == key)
        sample_rows.append(dict(bank=key[0], start=key[1], end=key[2], mulaw=key[3], sound_ram_address=address,
                                format='pcm16' if is16 else 'pcm8', samples=length,
                                bytes=length * (2 if is16 else 1), uses=uses))
    return dict(
        source=dict(vgm=str(vgm_path), vgm_sha256=hashlib.sha256(Path(vgm_path).read_bytes()).hexdigest()),
        window=dict(start_s=args.start, end_s=args.end, loop_ticks=r['loop_ticks'],
                    tick_seconds=TICK_SECONDS),
        c352=dict(register_writes=len(window_events), notes=len(r['notes_all']), silent_notes=len(r['silent']),
                  converted_notes=len(notes), peak_polyphony=r['poly'][0], peak_polyphony_at_s=r['poly'][1],
                  volume_or_pitch_changes=raw_changes, **change_stats(notes, r['info']['out_rate']),
                  loop_seams=loop_seams(notes, rom_image(r['info'])),
                  looped_notes=sum(1 for n in notes if n.looped),
                  mulaw_notes=sum(1 for n in notes if n.key[3]),
                  unique_samples=len(r['addresses'])),
        scsp=dict(events=len(r['events']), score_capacity=SCORE_MAX_EVENTS,
                  fits_the_driver_score=len(r['events']) <= SCORE_MAX_EVENTS,
                  events_by_register=kinds, ticks_with_events=len({e[0] for e in r['events']}),
                  dropped_notes=len(r['dropped']), **r['reuse'],
                  slots_used=len({n.slot for n in r['placed']}),
                  pcm8_bytes=r['pcm8_len'], pcm16_bytes=r['pcm16_len'], bank_bytes=len(r['bank']),
                  pcm8_base=args.pcm8_base, pcm16_base=r['pcm16_base'],
                  gain_scale=r['scale'], peak_c352_gain=r['peak_gain'],
                  score_bytes=len(r['score']), score_sha256=hashlib.sha256(r['score']).hexdigest(),
                  bank_sha256=hashlib.sha256(r['bank']).hexdigest()),
        pages=page_stats(r),
        dynamic_bank=r['dynamic'][2] if r['dynamic'] else None,
        quantisation=dict(onset_error_ms_mean=sum(quant) / max(1, len(quant)), onset_error_ms_max=max(quant, default=0),
                          pitch_error_cents_max=max(map(abs, pitch_err), default=0),
                          pitch_error_cents_mean=sum(map(abs, pitch_err)) / max(1, len(pitch_err))),
        not_carried=r['issues'],
        samples=sample_rows)


def write_outputs(out, r, manifest, start, end, pcm8_base, relative_pages=False, paged=True):
    """bank.bin, score.bin (1024-event form, only if it fits), pages.bin, manifest.json, notes.json, score_meta.h."""
    out.mkdir(parents=True, exist_ok=True)
    (out / 'bank.bin').write_bytes(r['bank'])
    pages = paginate(r['events'], r['loop_ticks'], relative_pages)
    (out / 'pages.bin').write_bytes(pages_bytes(pages))
    dyn = r['dynamic']
    (out / 'loads.bin').write_bytes(loads_bytes(dyn[1], pcm8_base, r['bank']) if dyn else bytes(24))
    fits = len(r['events']) <= SCORE_MAX_EVENTS
    if not paged and not fits:
        raise ValueError('more than 1024 events: use the paged score')
    (out / 'score.bin').write_bytes(r['score'] if fits and not paged else b'')
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    (out / 'notes.json').write_text(json.dumps(
        [dict(t_on=n.t_on, voice=n.voice, slot=n.slot, intended_ms=(n.on_tick * TICK_SECONDS + T0 - n.t_on) * 1000,
              sample=list(n.key), looped=n.looped, carried=bool(n.carried)) for n in r['placed']], indent=1))
    (out / 'score_meta.h').write_text(
        '/* Generated by c352_scsp_convert.py; private, not for Git. */\n'
        f"#define C352_BANK_BASE {pcm8_base:#x}u\n#define C352_BANK_BYTES {dyn[2]['packed_extent_bytes'] + dyn[2]['packed_extent_bytes'] % 2 if dyn else len(r['bank'])}u\n"
        f"#define C352_FILE_BYTES {len(r['bank'])}u\n#define C352_DYNAMIC {1 if dyn else 0}u\n#define C352_LOADS {len(dyn[1]) if dyn else 0}u\n"
        f"#define C352_SCORE_EVENTS {len(r['events']) if fits and not paged else 0}u\n#define C352_LOOP_TICKS {r['loop_ticks']}u\n"
        f"#define C352_WINDOW_MS {round((end - start) * 1000)}u\n#define C352_TICK_SAMPLES {TICK_SAMPLES}u\n"
        f"#define C352_PAGES {len(pages)}u\n#define C352_PAGED {1 if paged else 0}u\n"
        f"#define C352_LEAD_TICKS {round(0.3 * 44100 / TICK_SAMPLES)}u\n")
    return pages


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('vgm')
    parser.add_argument('--start', type=float, default=0.0, help='window start (s); a note already sounding enters at its sample position')
    parser.add_argument('--end', type=float, required=True, help='window end (s)')
    parser.add_argument('--pcm8-base', type=lambda s: int(s, 0), default=0x3000, help='Sound RAM address of the bank')
    parser.add_argument('--scale', type=float, help='linear gain applied to every voice (default: 1 / loudest voice if above 1)')
    parser.add_argument('--tick-samples', type=int, default=256, choices=(256, 128, 64), help='driver tick length in samples')
    parser.add_argument('--legacy-score', action='store_true', help='write the 1024-event score instead of pages')
    parser.add_argument('--bank-budget', type=int, help='load the samples section by section into this many bytes of Sound RAM')
    parser.add_argument('--relative-pages', action='store_true', help='page events relative to the page time base instead of absolute')
    parser.add_argument('--out', type=Path, help='write the data files here')
    parser.add_argument('--dry-run', action='store_true', help='print the manifest only')
    args = parser.parse_args()
    r = convert(args.vgm, args.start, args.end, args.pcm8_base, args.scale, tick_samples=args.tick_samples, bank_budget=args.bank_budget)
    manifest = summary(r, args, args.vgm)
    if args.out and not args.dry_run:
        write_outputs(args.out, r, manifest, args.start, args.end, args.pcm8_base, args.relative_pages, not args.legacy_score)
    json.dump({k: v for k, v in manifest.items() if k != 'samples'}, sys.stdout, indent=2)
    print()


if __name__ == '__main__':
    main()
