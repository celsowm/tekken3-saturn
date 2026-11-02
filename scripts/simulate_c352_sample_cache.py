#!/usr/bin/env python3
"""Simulate a Sound RAM sample cache for a C352 VGM on the Saturn SCSP.

A sample must be resident from the start of its load until its last playing note ends, so no live voice ever
loses its data. Uses of one sample closer than --gap seconds share one residency; a longer gap evicts it and
loads it again. The VGM loops (its loop start is replayed after its end), so two passes are simulated and
the transfers of the second are the steady state. Space is placed first-fit with 2-byte alignment, so the reported
extent includes fragmentation. mu-law samples can be expanded to PCM16 (twice the bytes).
CD model (estimates, parameters): drive bytes/s and seek seconds per transfer."""
import argparse
import json
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
import analyze_c352_vgm as A  # noqa: E402


def build_uses(path, mulaw16, skip_silent):
    info = A.parse(A.read_vgm(path))
    notes, samples, _, _ = A.analyze(info)
    if skip_silent:                         # a note that never has volume adds nothing to the output
        notes = [n for n in notes if n['audible']]
    total, loop_start = info['total'] / 44100, info['loop_start'] / 44100
    sizes = {}
    for n in notes:
        key = (n['bank'], n['wave_start'], n['wave_end'], n['flags'] & (A.MULAW | A.LOOP | A.REVERSE | A.LINK))
        length = ((key[2] - key[1]) & 0xFFFF) + 1
        sizes[key] = length * 2 if (key[3] & A.MULAW and mulaw16) else length
        n['key'] = key
    shift = total - loop_start
    second = [dict(n, start=n['start'] + shift, end=n['end'] + shift) for n in notes if n['end'] > loop_start]
    return notes + second, sizes, total, loop_start


def residencies(notes, sizes, gap, lead_of):
    by = {}
    for n in notes:
        by.setdefault(n['key'], []).append((n['start'], n['end']))
    out = []
    for key, uses in by.items():
        uses.sort()
        begin, end = uses[0]
        for s, e in uses[1:]:
            if s - end > gap:
                out.append((key, begin, end))
                begin, end = s, e
            else:
                end = max(end, e)
        out.append((key, begin, end))
    return [(key, b - lead_of(sizes[key]), b, e) for key, b, e in out]


class FirstFit:
    def __init__(self):
        self.blocks, self.extent = [], 0              # (address, size), sorted by address

    def alloc(self, size):
        size = (size + 1) & ~1
        addr = 0
        for a, s in self.blocks:
            if a - addr >= size:
                break
            addr = a + s
        self.blocks.append((addr, size))
        self.blocks.sort()
        self.extent = max(self.extent, addr + size)
        return addr

    def free(self, addr):
        self.blocks = [b for b in self.blocks if b[0] != addr]


def offline_extent(res, sizes):
    """Extent when the whole plan is known in advance (the VGM is fixed): place residencies largest first at the
    lowest address that is free for their whole lifetime. Ignores that the loop seam must reuse addresses."""
    placed = []                                   # (start, end, address, size)
    for key, begin, _, end in sorted(res, key=lambda r: -sizes[r[0]]):
        size = (sizes[key] + 1) & ~1
        busy = sorted((a, a + z) for b, e, a, z in placed if b < end and begin < e)
        addr = 0
        for a, z in busy:
            if a - addr >= size:
                break
            addr = max(addr, z)
        placed.append((begin, end, addr, size))
    return max((a + z for _, _, a, z in placed), default=0)


def simulate(path, gap, bandwidth, seek, mulaw16, skip_silent):
    notes, sizes, total, loop_start = build_uses(path, mulaw16, skip_silent)
    lead = lambda size: seek + size / bandwidth
    res = residencies(notes, sizes, gap, lead)
    events = sorted([(r[1], 1, i) for i, r in enumerate(res)] + [(r[3], 0, i) for i, r in enumerate(res)])
    mem, live, peak_live, peak_t = FirstFit(), {}, 0, 0
    live_bytes = 0
    for t, is_alloc, i in events:
        key = res[i][0]
        if is_alloc:
            live[i] = mem.alloc(sizes[key])
            live_bytes += sizes[key]
            if live_bytes > peak_live:
                peak_live, peak_t = live_bytes, t
        else:
            mem.free(live.pop(i))
            live_bytes -= sizes[key]
    pass2_from = total
    steady = [r for r in res if r[2] >= pass2_from]
    first = [r for r in res if r[2] < pass2_from]
    steady_bytes = sum(sizes[r[0]] for r in steady)
    return dict(gap_s=gap, peak_live_bytes=peak_live, peak_live_at_s=peak_t, first_fit_extent_bytes=mem.extent, offline_extent_bytes=offline_extent(res, sizes),
                loads_first_pass=len(first), bytes_first_pass=sum(sizes[r[0]] for r in first),
                loads_per_loop=len(steady), bytes_per_loop=steady_bytes,
                cd_bytes_per_s=steady_bytes / (total - loop_start),
                total_bytes_all_samples=sum(sizes.values()))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('vgm')
    p.add_argument('--gaps', type=float, nargs='+', default=[0, 2, 5, 10, 20, 60, 1e9])
    p.add_argument('--bandwidth', type=float, default=614400.0, help='CD bytes/s (2x drive; estimate)')
    p.add_argument('--seek', type=float, default=0.4, help='seconds per transfer (estimate)')
    p.add_argument('--mulaw-pcm16', action='store_true')
    p.add_argument('--skip-silent', action='store_true', help='drop notes whose volume is zero for their whole life')
    a = p.parse_args()
    json.dump([simulate(a.vgm, g, a.bandwidth, a.seek, a.mulaw_pcm16, a.skip_silent) for g in a.gaps], sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    main()
