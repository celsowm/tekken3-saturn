#!/usr/bin/env python3
"""Extract private pose tables using a fingerprinted upstream release layout.

The layout contains address metadata, not retail coefficients. Verify both
the full USA executable and its headerless body before trusting any mapping.
This establishes table provenance, not original-emulator pose parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from extract_psx_ik_tables import normalize, SIZES, TableError

CANONICAL = dict(sine=0x8001E8C4, sqrt=0x800990AC, basis=0x800972D0,
                 skeleton=0x800972F0, limb=0x80097320, spline=0x80096B00,
                 reactions=0x80010CE8,body_sqrt=0x8004B1D8,body_joints=0x80097364,body_radii=0x80097384,
                 common_branches=0x80017C78,atan=0x800210C8,push=0x8001075C,
                 rsqrt=0x8001A194-384,launch_push=0x80019D2C,air_kinds=0x8001A630,
                 close_reactions=0x800177DC,throw_victims=0x800174C4,
                 builtin_attacks=0x80017BDC)
BASE, HEADER = 0x80010000, 0x800
CLOSE_ENTRIES = 64
# g_throwVictimSlots: 397 s16 slots, then the 21 words a reaction read
# starting at the last entry reaches.
THROW_VICTIM_WORDS = 397 + 21
# The 13 built-in attack descriptors (id, pointer) at 0x80017C10 and the bytes
# their pointers reach: the game reads past a descriptor into what follows.
BUILTIN_ATTACK_BYTES = 0x400
BUILTIN_TABLE, BUILTIN_COUNT = 0x34, 13
REVERSAL_LISTS, REVERSAL_COUNT = 0x80095AB8, 52


def reversal_lists(raw, address):
    """Bounded USA pointers to positive s16 tag lists; offsets, not pointers."""
    def at(pointer, size):
        offset = pointer - BASE + HEADER
        if offset < HEADER or offset + size > len(raw):
            raise TableError('reversal animation list outside executable')
        return offset
    table = at(address, REVERSAL_COUNT * 4)
    values, offsets = bytearray(), []
    head = 2 + 2 * (REVERSAL_COUNT + 1)
    for index in range(REVERSAL_COUNT):
        pointer = struct.unpack_from('<I', raw, table + 4 * index)[0]
        offsets.append(head + len(values))
        for n in range(512):
            tag = struct.unpack_from('<h', raw, at(pointer + 2 * n, 2))[0]
            values += struct.pack('<h', max(0, tag))
            if tag <= 0:
                break
        else:
            raise TableError('unterminated reversal animation list')
    offsets.append(head + len(values))
    if offsets[-1] > 65535:
        raise TableError('reversal animation lists exceed offset format')
    return struct.pack('<H', REVERSAL_COUNT) + struct.pack('<53H', *offsets) + values


def builtin_attacks(window, address):
    """The window with the 13 descriptor pointers rewritten as window offsets."""
    window = bytearray(window)
    for i in range(BUILTIN_COUNT):
        at = BUILTIN_TABLE + 8 * i + 4
        pointer = struct.unpack_from('<I', window, at)[0]
        if not address <= pointer < address + BUILTIN_ATTACK_BYTES - 6:
            raise TableError(f'built-in attack descriptor {i} outside its window')
        struct.pack_into('<I', window, at, pointer - address)
    return bytes(window)


def mapped(layout, address, size):
    offset = address - BASE
    candidates = [source + offset - start for start, length, source in layout['segments']
                  if start <= offset and offset + size <= start + length]
    if len(candidates) != 1:
        raise TableError(f'no unambiguous complete segment for {address:#x}')
    source = candidates[0] + BASE
    points = dict(layout['points'])
    if address in points and points[address] != source:
        raise TableError(f'point/segment disagree at {address:#x}')
    return source


def references(body, targets):
    """Diagnostic static LUI/low-half references, stopped at register writes."""
    words = struct.unpack('<' + 'I' * (len(body) // 4), body[:len(body) // 4 * 4])
    found = {name: [] for name in targets}
    for i, word in enumerate(words[:0x80000 // 4]):
        if word >> 26 != 15:
            continue
        reg, hi = word >> 16 & 31, (word & 65535) << 16
        for j, low in enumerate(words[i + 1:i + 13], 1):
            op, rs, rt = low >> 26, low >> 21 & 31, low >> 16 & 31
            if rs == reg and op in (9, 13, 32, 33, 35, 36, 37, 40, 41, 43):
                value = low & 65535
                if op != 13 and value >= 32768:
                    value -= 65536
                target = (hi + value) & 0xffffffff
                for name, (address, size) in targets.items():
                    if address <= target < address + size:
                        found[name].append(dict(pc=BASE + 4 * (i + j), target=target))
            if rt == reg and op not in (40, 41, 43):
                break
    return found


def extract(exe, layout_file, output):
    raw = exe.read_bytes()
    layout = json.loads(layout_file.read_text())['blocks']['exe']
    if hashlib.sha256(raw[HEADER:]).hexdigest() != layout['sha256']:
        raise TableError('USA executable body does not match release layout')
    sizes = dict(SIZES, spline=0x660, common_branches=672*12,atan=1025,push=0x58C,body_sqrt=384,body_joints=32,body_radii=4,reactions=633*42,rsqrt=768,
                 launch_push=16,air_kinds=24,close_reactions=CLOSE_ENTRIES*4,
                 throw_victims=THROW_VICTIM_WORDS*2,builtin_attacks=BUILTIN_ATTACK_BYTES)
    targets = {name: (mapped(layout, address, sizes[name]), sizes[name])
               for name, address in CANONICAL.items()}
    offsets = {name: address - BASE + HEADER for name, (address, _) in targets.items()}
    trig, ik, manifest = normalize(raw, {name: offsets[name] for name in SIZES})
    refs = references(raw[HEADER:], targets)
    if any(not refs[name] for name in ('sine', 'sqrt', 'basis', 'skeleton', 'spline')):
        raise TableError('expected pose table code reference is missing')
    # VectorNormal addresses both the regular reciprocal-sqrt table and its
    # preceding bytes when signed squared length wraps. Require a reference
    # into the actual table, not merely the preceding part of this window.
    rsqrt_address=targets['rsqrt'][0]+384
    if not any(rsqrt_address<=r['target']<rsqrt_address+384 for r in refs['rsqrt']):
        raise TableError('expected reciprocal-sqrt table code reference is missing')
    # The four limb offsets are the next 12 signed words after the skeleton
    # offsets, in the same fingerprinted contiguous layout segment.
    if offsets['limb'] != offsets['skeleton'] + SIZES['skeleton']:
        raise TableError('limb/skeleton table adjacency changed')
    spline = raw[offsets['spline']:offsets['spline'] + sizes['spline']]
    branches = raw[offsets['common_branches']:offsets['common_branches'] + sizes['common_branches']]
    # Ground blob: atan, push tables, the launch push table and the air kinds
    # (t3_psx_ground_tables_bind).
    ground=b''.join(raw[offsets[k]:offsets[k]+sizes[k]] for k in ('atan','push','launch_push','air_kinds'))
    if struct.unpack_from('<H', branches, len(branches)-12)[0] != 0xC00D:
        raise TableError('common branch table must end with RETURN')
    output.mkdir(parents=True, exist_ok=True)
    for name, data in [('psx_trig.bin', trig), ('psx_ik.bin', ik), ('psx_spline.bin', spline),
                       ('psx_common_branches.bin', branches),
                       ('psx_rsqrt.bin',raw[offsets['rsqrt']:offsets['rsqrt']+sizes['rsqrt']]),
                       ('psx_atan.bin',raw[offsets['atan']:offsets['atan']+sizes['atan']]),
                       ('psx_push.bin',raw[offsets['push']:offsets['push']+sizes['push']])]:
        (output / name).write_bytes(data)
    (output/'psx_ground.bin').write_bytes(ground)
    # The 633 reaction records, then g_closeReactions (s16 reaction, s16
    # distance per move +0x34 index; the banks use indices up to 0x39),
    # then g_throwVictimSlots (move +0x32 values 0x1nnn), then the built-in
    # attack descriptor window (move +0x28 ids).
    reactions=b''.join(raw[offsets[k]:offsets[k]+sizes[k]] for k in ('reactions','close_reactions','throw_victims'))
    reactions+=builtin_attacks(raw[offsets['builtin_attacks']:offsets['builtin_attacks']+BUILTIN_ATTACK_BYTES],
                               targets['builtin_attacks'][0])
    (output/'psx_reactions.bin').write_bytes(reactions)
    reversal_address = mapped(layout, REVERSAL_LISTS, REVERSAL_COUNT * 4)
    reversal = reversal_lists(raw, reversal_address)
    (output/'psx_reversal_lists.bin').write_bytes(reversal)
    manifest['reversal_lists_address'] = reversal_address
    manifest['reversal_lists_sha256'] = hashlib.sha256(reversal).hexdigest()
    manifest['reactions_sha256']=hashlib.sha256(reactions).hexdigest()
    body_sqrt=raw[offsets['body_sqrt']:offsets['body_sqrt']+384]
    body_joints=raw[offsets['body_joints']:offsets['body_joints']+32]
    radii_pointer=struct.unpack_from('<I',raw,offsets['body_radii'])[0]
    radii_offset=radii_pointer-BASE+HEADER
    if not HEADER<=radii_offset<=len(raw)-16:raise TableError('Paul body radii outside EXE')
    body=body_sqrt+body_joints+raw[radii_offset:radii_offset+16]
    (output/'psx_body.bin').write_bytes(body)
    (output/'psx_body_sqrt.bin').write_bytes(body_sqrt)
    manifest['body_sha256']=hashlib.sha256(body).hexdigest()
    manifest['rsqrt_sha256']=hashlib.sha256(raw[offsets['rsqrt']:offsets['rsqrt']+768]).hexdigest()
    manifest['rsqrt_table_address']=rsqrt_address
    manifest['paul_body_radii_address']=radii_pointer
    manifest.update(layout_sha256=hashlib.sha256(layout_file.read_bytes()).hexdigest(),
                    layout_body_sha256=layout['sha256'], source_addresses=targets,
                    code_references=refs, spline_sha256=hashlib.sha256(spline).hexdigest(),
                    common_branches_sha256=hashlib.sha256(branches).hexdigest(),
                    ground_sha256=hashlib.sha256(ground).hexdigest(),
                    source='Fingerprint-checked USA layout segments, points and static code references',
                    original_emulator_pose_parity=False)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', required=True, type=Path)
    p.add_argument('--layout', type=Path, default=Path(__file__).resolve().parents[1] /
                   'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    result = extract(a.exe, a.layout, a.output)
    print(json.dumps(result, indent=2))
