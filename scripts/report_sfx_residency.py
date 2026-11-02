#!/usr/bin/env python3
"""Inventory of the sounds a Tekken 3 Saturn fight keeps resident, with duplicates, silence, request counts.

Reads the private build outputs (build/generated: SFX.BIN, t3_local_sfx.h, F<nn>.BIN packs, roster.json), optionally
the USA executable (voice roles) and the request counts written by scripts/measure_sfx_requests.py. Prints tables
and, with --json, the same data. It writes no audio and keeps nothing proprietary: only sizes, hashes and counts.
Residency (bytes in Sound RAM) is a different quantity from polyphony (voices playing at once)."""
import argparse
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
SECTIONS = ('hot', 'archive', 'bank', 'camera', 'flipbook', 'voices', 'pcm', 'names')
DIRECTORY_WORD = 26
LOW = 2                                     # |sample| <= 2 of 127 (-36 dBFS) counts as quiet


def aligned(n):
    return (n + 1) & ~1


def run_length(samples, from_end, limit):
    n = 0
    it = reversed(samples) if from_end else samples
    for s in it:
        if abs(s) > limit:
            break
        n += 1
    return n


def half_rate_snr_db(samples):
    """Proxy for how much a sound loses if stored at half its rate: pair-average, then linear interpolation."""
    x = list(samples)
    if len(x) < 8:
        return 0.0
    y = [(x[i] + x[i + 1]) / 2 for i in range(0, len(x) - 1, 2)]
    err = energy = 0.0
    for k, v in enumerate(y):
        nxt = y[k + 1] if k + 1 < len(y) else v
        for j, r in ((2 * k, v), (2 * k + 1, (v + nxt) / 2)):
            if j < len(x):
                err += (x[j] - r) ** 2
                energy += x[j] ** 2
    return 99.0 if err == 0 else round(10 * math.log10(energy / err), 1) if energy else 0.0


def metrics(raw):
    s = [b - 256 if b > 127 else b for b in raw]
    return dict(bytes=len(raw), sha1=hashlib.sha1(raw).hexdigest()[:12], peak=max((abs(v) for v in s), default=0),
                lead0=run_length(s, False, 0), tail0=run_length(s, True, 0),
                lead_quiet=run_length(s, False, LOW), tail_quiet=run_length(s, True, LOW), half_snr_db=half_rate_snr_db(s))


def shared_sounds(generated):
    text = (generated / 'private_sfx/t3_local_sfx.h').read_text()
    names = {int(i): n for n, i in re.findall(r'#define T3_SFX_(\w+)\s+(\d+)u', text) if n not in ('FILE_BYTES', 'COUNT', 'MAX_SAMPLES')}
    block = lambda name: re.search(r'define %s\s+(.*?)(?=\n#define|\Z)' % name, text, re.S).group(1)
    table = [tuple(map(int, r)) for r in re.findall(r'\{\s*(\d+)u?,\s*(\d+)u?,\s*(\d+)u?\s*\}', block('T3_SFX_TABLE'))]
    keys = re.findall(r'\{\s*(\d+)u?,\s*(\d+)u?,\s*0x([0-9A-Fa-f]+)u?\s*\}', block('T3_SFX_KEY_TABLE'))
    data = (generated / 'private_sfx/SFX.BIN').read_bytes()
    rows = []
    for i, ((offset, samples, rate), (bank, _, code)) in enumerate(zip(table, keys)):
        rows.append(dict(index=i, name=names.get(i, '?'), bank=int(bank), code=int(code, 16), rate=rate,
                         **metrics(data[offset:offset + samples])))
    return rows


def voice_roles(exe, char):
    import extract_psx_sfx as E
    off = lambda a: a - 0x80010000 + 0x800
    p0, p1, p2 = struct.unpack_from('<III', exe, 0xB554 + 24 * char)
    counts = struct.unpack_from('<4h', exe, off(p0))
    codes = struct.unpack_from('<%dH' % sum(counts), exe, off(p0) + 8)
    roles, at = {}, 0
    for c, n in enumerate(counts):
        for code in codes[at:at + n]:
            roles.setdefault(code, set()).add(f'cat{c + 2}')
        at += n
    shouts, damage, _ = E.voice_codes(exe, char)
    for code in shouts:
        roles.setdefault(code, set()).add('shout')
    for code in damage:
        roles.setdefault(code, set()).add('damage')
    return {code: '+'.join(sorted(r)) for code, r in roles.items()}


def pack_voices(generated, fighter, exe):
    data = (generated / 'fighters' / fighter['file']).read_bytes()
    words = struct.unpack_from('>%dI' % (DIRECTORY_WORD + 2 * len(SECTIONS)), data, 0)
    voice_set = words[4]
    d = {name: (words[DIRECTORY_WORD + 2 * i], words[DIRECTORY_WORD + 2 * i + 1]) for i, name in enumerate(SECTIONS)}
    table = data[d['voices'][0]:d['voices'][0] + d['voices'][1]]
    pcm = data[d['pcm'][0]:d['pcm'][0] + d['pcm'][1]]
    roles = voice_roles(exe, voice_set) if exe else {}
    rows = []
    for i in range(0, len(table), 16):
        code, offset, samples, rate = struct.unpack_from('>IIII', table, i)
        rows.append(dict(index=i // 16, code=code, rate=rate, role=roles.get(code, '?'),
                         **metrics(pcm[offset:offset + samples])))
    return rows


def requests(path, shared_count):
    """Per sound: total requests over all runs, runs that requested it, and the largest count in one run."""
    runs = [r for r in json.loads(path.read_text()) if 'error' not in r]
    def agg(values):
        return dict(total=sum(values), runs_with=sum(1 for v in values if v), max_run=max(values, default=0))
    out = dict(runs=len(runs), seconds=sum(r['fields'] for r in runs) / 60, shared=[], voice=[[], []])
    for i in range(shared_count):
        out['shared'].append(agg([r['sfx_shared_plays'][i] for r in runs]))
    width = len(runs[0]['sfx_voice_plays']) // 2
    for actor in (0, 1):
        for i in range(width):
            out['voice'][actor].append(agg([r['sfx_voice_plays'][actor * width + i] for r in runs]))
    return out


def budget(everything, dupes, freq):
    """Bytes each lever frees for the resident set of the pair. Levers marked lossy change the sound."""
    total = sum(aligned(r['bytes']) for _, r in everything)
    size_of = {r['sha1']: aligned(r['bytes']) for _, r in everything}
    dup = sum(size_of[h] * (len(v) - 1) for h, v in dupes.items())
    ann = {r['name']: aligned(r['bytes']) for o, r in everything if o == 'shared' and r['name'].startswith('ANN_')}
    first = ann['ANN_ROUND'] + max(ann['ANN_ONE'], ann['ANN_TWO'], ann['ANN_THREE']) + ann['ANN_FIGHT']
    never = sum(aligned(r['bytes']) for _, r in everything if r['requests'] and r['requests']['total'] == 0)
    tail0 = sum(r['tail0'] for _, r in everything)
    lead0 = sum(r['lead0'] for _, r in everything)
    tailq = sum(r['tail_quiet'] for _, r in everything)
    zero = lambda r: r['requests'] and r['requests']['total'] == 0
    seen = set()
    rows = []
    for o, r in everything:                               # one copy of each exact duplicate
        if r['sha1'] not in seen:
            seen.add(r['sha1'])
            rows.append((o, r))
    annb = lambda r: r.get('name', '').startswith('ANN_')
    s1 = sum(aligned(r['bytes']) for o, r in rows)
    s2 = s1 - sum(aligned(r['bytes']) for o, r in rows if annb(r))
    s3 = s2 - sum(aligned(r['bytes']) for o, r in rows if zero(r) and not annb(r))
    late = lambda r: any(c in r.get('role', '') for c in ('cat4', 'cat5')) and 'shout' not in r['role'] and 'damage' not in r['role']
    s4 = s3 - sum(aligned(r['bytes']) for o, r in rows if late(r) and not zero(r))
    print(f'scenarios: all {total}; 1 dedupe {s1}; 2 +announcer out of combat {s2}; 3 +never-requested out {s3} [needs proof]; '
          f'4 +cat4/cat5 voices out {s4} [role assumption]')
    print(f'resident {total}; exact duplicates {dup}; announcer {sum(ann.values())} (one round needs {first} at most); '
          f'never requested {never}; exact-zero tails {tail0}, leads {lead0}; quiet (<={LOW}) tails {tailq} [lossy]')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--generated', type=Path, default=ROOT / 'build/generated')
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--requests', type=Path, help='runs.json from measure_sfx_requests.py')
    p.add_argument('--pair', nargs=2, default=['PAUL', 'LAW'])
    p.add_argument('--json', action='store_true')
    args = p.parse_args()
    roster = {f['name']: f for f in json.loads((args.generated / 'fighters/roster.json').read_text())}
    exe = args.exe.read_bytes() if args.exe.is_file() else None
    shared = shared_sounds(args.generated)
    voices = [pack_voices(args.generated, roster[n], exe) for n in args.pair]
    freq = requests(args.requests, len(shared)) if args.requests else None
    for rows, f in ((shared, freq and freq['shared']),) + tuple((voices[a], freq and freq['voice'][a]) for a in (0, 1)):
        for i, row in enumerate(rows):
            row['requests'] = f[i] if f else None
    everything = [('shared', r) for r in shared] + [(n, r) for n, rows in zip(args.pair, voices) for r in rows]
    by_hash = {}
    for owner, r in everything:
        by_hash.setdefault(r['sha1'], []).append((owner, r.get('name') or hex(r['code'])))
    dupes = {h: v for h, v in by_hash.items() if len(v) > 1}
    # every pack, to see how much the roster repeats itself
    allpacks = {}
    for f in roster.values():
        if f['voices']:
            for r in pack_voices(args.generated, f, None):
                allpacks.setdefault(r['sha1'], r['bytes'])
    report = dict(pair=args.pair, shared=shared, voices=dict(zip(args.pair, voices)), requests=freq,
                  duplicates={h: v for h, v in dupes.items()},
                  roster_unique_voice_bytes=sum(allpacks.values()),
                  roster_voice_bytes=sum(aligned(f['pcm_bytes']) for f in roster.values()))
    if args.json:
        json.dump(report, sys.stdout, indent=1)
        print()
        return
    def line(owner, r):
        q = r['requests']
        req = f"{q['total']:>6} {q['runs_with']:>3}/{freq['runs']}" if q else ''
        return (f"{owner:<6} {r.get('name') or hex(r['code']):<12} {r['code']:#06x} {r.get('role', ''):<12} {r['bytes']:>7} "
                f"{r['rate']:>6} {r['bytes'] / r['rate'] * 1000:>6.0f}ms {r['sha1']} lead0={r['lead0']:<5} tail0={r['tail0']:<5} "
                f"quiet={r['lead_quiet']}/{r['tail_quiet']} half={r['half_snr_db']:>5} {req}")
    for owner, r in everything:
        print(line(owner, r))
    budget(everything, dupes, freq)
    print('shared bytes', sum(r['bytes'] for r in shared), 'voice bytes', [sum(r['bytes'] for r in v) for v in voices])
    print('duplicates', json.dumps(dupes))
    print('roster voice bytes', report['roster_voice_bytes'], 'unique', report['roster_unique_voice_bytes'])


if __name__ == '__main__':
    main()
