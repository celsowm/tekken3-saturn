#!/usr/bin/env python3
"""Extract the combat sound effects the Saturn build plays from the USA executable.

Reads VAB 0 (the combat bank in SLUS_004.02: VH at 0x16050, body at 0xC3E6C),
decodes the PS-ADPCM samples of a fixed list of sound codes and writes one
private blob plus a table: `psx_sfx.bin` is 8-bit signed PCM, `psx_sfx.tsv` has
`name code vag rate offset samples` per sound. Sound codes, playback rate and
the ADPCM rule follow third_party/Tekken3Remake/docs/research/{code/sound.md,
formats/vab-banks.md} (PolyForm Noncommercial; rules re-implemented here, see
NOTICE.md). The output is retail data: keep it under assets/imported/.
"""
import argparse
import struct
from pathlib import Path

VH, VB, VAB_BYTES = 0x16050, 0xC3E6C, 252880
# name -> sound code (bits 4-5 program, bits 0-3 tone; bank 1 = VAB 0)
SOUNDS = {
    'swing': 0x604A,        # id 0x4A, one frame before an attack's active window
    'guard_hand': 0x704D,   # blocked hit from joints below 12
    'guard_foot': 0x704E,   # blocked hit from joint 12 and up
    'hit': 0x61A0,          # id 0x81, clean-hit impact
}
# The system bank (VAB 1, VH at 0x17270, body at 0x10081c): the round announcer.
SYS_VH, SYS_VB, SYS_BYTES = 0x17270, 0x10081C, 103552
ANNOUNCER = {
    'ann_round': 0x870D,    # id 0x3B, before the round number
    'ann_final': 0x86CC,    # id 0x3A, the deciding round
    'ann_one': 0x86D0,      # ids 0x34-0x36
    'ann_two': 0x8711,
    'ann_three': 0x8652,
    'ann_fight': 0x8781,    # id 0x3C
}
FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def decode(frames):
    """PS-ADPCM frames -> 16-bit samples; the zero lead-in and IRQ end block are dropped."""
    prev = older = 0
    out = []
    for i in range(0, len(frames), 16):
        f = frames[i:i + 16]
        shift, flt, flag = f[0] & 15, f[0] >> 4, f[1]
        if flag == 7 or (f[1] == 0x07 and f[2:] == b'\x77' * 14):
            break
        c1, c2 = FILTERS[min(flt, 4)]
        for byte in f[2:]:
            for nib in (byte & 15, byte >> 4):
                r = nib - 16 if nib >= 8 else nib
                v = (r << 12) >> shift
                t = prev * c1 + older * c2
                v += int(t / 64)
                older, prev = prev, v
                out.append(max(-32768, min(32767, v)))
    return out[28:]


def rate(center, shift, key):
    n = key - (center - 60) + ((shift >> 3) >> 4)
    pitch = int(4096 * 2 ** (((n % 12) * 16 + ((shift >> 3) & 15)) / 192)) >> (5 - n // 12)
    return 44100 * pitch // 4096


def vab_sound(vh, body, code):
    """Decode the tone `code` selects (program bits 4-5, tone bits 0-3) from a VAB."""
    programs, vags = vh[0x12], struct.unpack_from('<H', vh, 0x16)[0]
    sizes = struct.unpack_from('<256H', vh, 0x820 + programs * 0x200)
    starts = [0]
    for size in sizes[1:vags]:
        starts.append(starts[-1] + (size << 3))
    prog, tone = (code >> 4) & 3, code & 15
    slot = vh[0x820 + prog * 0x200 + tone * 32:][:32]
    vag = struct.unpack_from('<H', slot, 22)[0]
    samples = decode(body[starts[vag - 1]:starts[vag - 1] + (sizes[vag] << 3)])
    return vag, rate(slot[4], slot[5], 60 + tone), samples


def voice_codes(d, char):
    """The fighter's own sound codes: (attack shouts, damage voices, a KO voice: category 2, as FighterSounds draws 0x4nnn) from g_charVoices."""
    off = lambda a: a - 0x80010000 + 0x800
    p0, p1, p2 = struct.unpack_from('<III', d, 0xB554 + 24 * char)
    counts = struct.unpack_from('<4h', d, off(p0))
    codes = struct.unpack_from('<%dH' % sum(counts), d, off(p0) + 8)
    lists, at = [], 0
    for n in counts:
        lists.append(codes[at:at + n])
        at += n
    resolve = lambda voice: lists[(voice >> 12) - 2][voice & 0xFFF]
    shouts_n = struct.unpack_from('<h', d, off(p1))[0]
    shouts = struct.unpack_from('<%dH' % shouts_n, d, off(p1) + 2)
    light, heavy = struct.unpack_from('<2h', d, off(p2))
    damage = struct.unpack_from('<%dH' % (light + heavy), d, off(p2) + 4)
    unique = lambda seq: list(dict.fromkeys(resolve(v) for v in seq))
    return unique(shouts)[:2], unique(damage)[:3], list(dict.fromkeys(lists[2]))[:1]


def arc_member(arc, index):
    offset, size = struct.unpack_from('<II', arc, 4 + 8 * index)
    return arc[offset:offset + size]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, default=Path('assets/imported/tekken3-usa/SLUS_004.02'))
    p.add_argument('--out', type=Path, default=Path('assets/imported/tekken3-usa/sfx'))
    p.add_argument('--voice', action='append', default=[], metavar='SLOT:CHAR:VH:ARC',
                   help='a fighter slot\'s character id, voice header (BNS 72+4n) and ARC (73+4n); '
                        'adds f<slot>_shout0/1 and f<slot>_hurt0..2')
    p.add_argument('--generated', type=Path,
                   help='also write SFX.BIN and t3_local_sfx.h (the Saturn build\'s inputs) here')
    args = p.parse_args()
    d = args.exe.read_bytes()
    vh = d[VH:VH + 0x1220]
    if vh[:4] != b'pBAV' or struct.unpack_from('<I', vh, 12)[0] != VAB_BYTES:
        raise SystemExit('not the USA VAB 0')
    jobs = [(name, vh, d[VB:VB + VAB_BYTES - 0x1220], code) for name, code in SOUNDS.items()]
    sys_vh_len = 0x820 + d[SYS_VH + 0x12] * 0x200 + 0x200
    sys_vh = d[SYS_VH:SYS_VH + sys_vh_len]
    if sys_vh[:4] != b'pBAV' or struct.unpack_from('<I', sys_vh, 12)[0] != SYS_BYTES:
        raise SystemExit('not the USA VAB 1')
    jobs += [(name, sys_vh, d[SYS_VB:SYS_VB + SYS_BYTES - sys_vh_len], code)
             for name, code in ANNOUNCER.items()]
    for spec in args.voice:
        slot, char, vh_path, arc_path = spec.split(':', 3)
        char_vh, body = Path(vh_path).read_bytes(), arc_member(Path(arc_path).read_bytes(), 2)
        shouts, hurts, kos = voice_codes(d, int(char))
        jobs += [(f'f{slot}_shout{i}', char_vh, body, c) for i, c in enumerate(shouts)]
        jobs += [(f'f{slot}_hurt{i}', char_vh, body, c) for i, c in enumerate(hurts)]
        jobs += [(f'f{slot}_ko{i}', char_vh, body, c) for i, c in enumerate(kos)]
    pcm, rows = bytearray(), []
    for name, job_vh, body, code in jobs:
        vag, hz, samples = vab_sound(job_vh, body, code)
        while len(pcm) & 3:
            pcm.append(0)
        rows.append((name, code, vag, hz, len(pcm), len(samples)))
        pcm += bytes((v >> 8) & 255 for v in samples)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'psx_sfx.bin').write_bytes(pcm)
    (args.out / 'psx_sfx.tsv').write_text(''.join('\t'.join(map(str, r)) + '\n' for r in rows))
    if args.generated:
        args.generated.mkdir(parents=True, exist_ok=True)
        (args.generated / 'SFX.BIN').write_bytes(pcm)
        lines = ['#ifndef T3_LOCAL_SFX_H', '#define T3_LOCAL_SFX_H',
                 f'#define T3_SFX_FILE_BYTES {len(pcm)}u', f'#define T3_SFX_COUNT {len(rows)}u',
                 f'#define T3_SFX_MAX_SAMPLES {max(r[5] for r in rows)}u']
        lines += [f'#define T3_SFX_{r[0].upper()} {i}u' for i, r in enumerate(rows)]
        lines += ['#define T3_SFX_TABLE \\'] + [f'    {{{r[4]}u,{r[5]}u,{r[3]}u}}, \\' for r in rows]
        lines[-1] = lines[-1].rstrip(' \\')
        (args.generated / 't3_local_sfx.h').write_text('\n'.join(lines + ['#endif', '']))
    for r in rows:
        print(*r)


if __name__ == '__main__':
    main()
