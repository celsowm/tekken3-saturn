#!/usr/bin/env python3
"""Build one private disc file per selectable fighter (Fnn.BIN) and the roster limits.

A pack holds what the Saturn loads for one fighter after the character select:
the hot tables, the SKA1 mesh archive, the logic bank, camera streams, flipbook,
voice samples and name sheets. src/saturn/fighter_pack.c reads it. The header is
big-endian. t3_roster.h gives the largest counts, which size the fixed buffers.
The output is retail-derived: keep it under assets/imported or build/.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from build_local_camera_assets import CHOICE_SECTION, STREAM_SECTION, compact_streams
from build_local_kmd_assets import compile_mesh, projected_mesh, skeleton_parts
from divmot_to_logic import sections_of
from extract_psx_char_flipbook import flipbook_file
from extract_psx_hud import extract_name, system_textures
from extract_psx_sfx import all_voice_codes, arc_member, vab_sound
from build_stage_packs import STAGE_RECORDS, STAGE_RECORD_BYTES, fighter_home, usa_file_offset
from fighter_roster import COMMON_RECORD, ROSTER, STAGE_NUMBERS, bank_record, body_radii, filter_words, records
from kmd_to_ska import HAND_VARIANTS, parse_kmd
from psx_character_textures import load_character_vram

SCRIPTS = Path(__file__).resolve().parent
MAGIC = 0x54334650
VERSION = 6
# Tone classes of the lighting data: Gouraud offsets are additive, so each face's offset scales
# with its class's mean texel colour (see lighting_data).
TONE_CLASSES = 16
HEADER_BYTES = 256
SECTIONS = ('hot', 'archive', 'bank', 'camera', 'flipbook', 'voices', 'pcm', 'names')
VOICE_ROW_BYTES = 16
SKA_FIELD_PALETTES, SKA_FIELD_TEXTURES, SKA_FIELD_TEXTURE_OFFSET = 9, 8, 19
SKA_TEXTURE_BYTES = 16
POST_ARRAYS = 6


def fnv1a(data, value=2166136261):
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def align(blob, boundary):
    while len(blob) % boundary:
        blob.append(0)


def camera_data(raw_bank):
    _, sec = sections_of(raw_bank)
    choices = [struct.unpack_from('<hHi', raw_bank, a)
               for a in range(sec[CHOICE_SECTION], sec[CHOICE_SECTION + 1] - 7, 8)]
    entries, encoded = compact_streams(raw_bank, sec[STREAM_SECTION], choices)
    return (b''.join(struct.pack('>hHi', *c) for c in choices), len(choices),
            b''.join(struct.pack('>3HxxIHxx', *e) for e in entries), len(entries), encoded)


def voices(exe, fighter, vab, arc):
    """The voice samples of one fighter: the table rows (code, offset, samples, rate) and PCM."""
    body = arc_member(arc, 2)
    codes = all_voice_codes(exe, fighter.voice_set)
    table, pcm = bytearray(), bytearray()
    for code in codes:
        _, hz, samples = vab_sound(vab, body, code)
        align(pcm, 4)
        table += struct.pack('>IIII', code, len(pcm), len(samples), hz)
        pcm += bytes((v >> 8) & 255 for v in samples)
    return bytes(table), bytes(pcm), len(codes)


def names(arc, width, system_vram):
    """Both name sheets: the original's P1 art is flush left and its P2 art flush right."""
    vram, _ = load_character_vram(arc)
    out = bytearray(struct.pack('>HHH', width, (width + 7) & ~7, 16))
    align(out, 4)
    for side in ('left', 'right'):
        _, pixels, lut, _ = extract_name(vram, system_vram, ('name', None, width, side))
        out += lut + pixels
    return bytes(out)


def projected_table(mesh, put):
    """t3_psx_projected_part_t rows; put() places an array and returns its hot
    offset, and the Saturn turns non-zero offsets into pointers (parts come first)."""
    rows = bytearray()

    def array(fmt, values):
        return put(struct.pack(f'>{len(values)}{fmt}', *values), 2) if values else 0

    for block in mesh:
        vb = block['vert']
        coords = [c for row in vb.coords for c in row]
        posts = list(vb.posts) + [()] * (POST_ARRAYS - len(vb.posts))
        words = [block['part'], array('B', vb.imports_local), len(vb.imports_local),
                 array('B', vb.imports_global), len(vb.imports_global),
                 array('h', coords), len(vb.coords),
                 *(array('H', p) for p in posts), *(len(p) for p in posts),
                 array('H', block['map']), array('H', block['logical']), len(block['entries'])]
        rows += struct.pack(f'>{len(words)}I', *words)
    return bytes(rows)


def tone_classes(tones, k=TONE_CLASSES):
    """Lloyd's k-means over the faces' mean 5-bit colours, seeded at luminance quantiles."""
    order = sorted(range(len(tones)), key=lambda i: sum(tones[i]))
    centres = [tones[order[(2 * j + 1) * len(order) // (2 * k)]] for j in range(k)]
    labels = [0] * len(tones)
    for _ in range(24):
        labels = [min(range(k), key=lambda j: sum((a - b) ** 2 for a, b in zip(t, centres[j]))) for t in tones]
        for j in range(k):
            members = [tones[i] for i, label in enumerate(labels) if label == j]
            if members:
                centres[j] = tuple(sum(m[c] for m in members) / len(members) for c in range(3))
    return labels, centres


def lighting_data(lighting):
    """The faces' lit normals for the runtime relight (fighter_light.c).

    normals: (x, y, z, joint) int16 each, Q12 unit vectors in the joint's basis.
    faces: one word per face: bits 0-9 the normal of corner A, bits 10-13 the tone class, bit 15
    set when one normal lights the whole face; otherwise `corners` holds the normals of its
    corners B, C, D, three words per such face in face order.
    tones: per class the mean texel colour, RGB555."""
    normals, faces = lighting['normals'], lighting['faces']
    if len(normals) > 1024:
        raise ValueError('more than 1024 lit normals')
    labels, centres = tone_classes(lighting['tones'])
    words, corners = [], []
    for face, label in zip(faces, labels):
        flat = len(set(face)) == 1
        words.append(face[0] | label << 10 | (0x8000 if flat else 0))
        if not flat:
            corners += face[1:]
    return (b''.join(struct.pack('>hhhH', x, y, z, joint) for joint, x, y, z in normals),
            struct.pack(f'>{len(words)}H', *words), struct.pack(f'>{len(corners)}H', *corners),
            struct.pack(f'>{len(centres)}H', *(round(r) | round(g) << 5 | round(b) << 10 for r, g, b in centres)),
            len(normals))


def hand_variant_rows(variants, mesh, put):
    """The hand-variant rows (kmd_to_ska.hand_variants) as t3_hand_variant_row_t records, their
    arrays placed with `put`: channel, projected block, the moved vertices and changed skin
    records (record << 2 | influence), then per variant 0-9 and the base the vertices' KMD
    positions and the records' skin positions (zero for a variant that keeps the block)."""
    blocks = {(b['part'], b['row']): i for i, b in enumerate(mesh)}
    rows = b''
    for v in variants:
        present = sum(1 << i for i, c in enumerate(v['coords'][:HAND_VARIANTS]) if c is not None)
        zero_v, zero_r = [(0, 0, 0)] * len(v['vertices']), [(0, 0, 0)] * len(v['records'])
        coords = [c if c is not None else zero_v for c in v['coords']]
        positions = [p if p is not None else zero_r for p in v['positions']]
        if any(r >= 1 << 14 or k > 2 for r, k in v['records']):
            raise ValueError('hand variant record out of range')
        at = [put(struct.pack(f">{len(v['vertices'])}H", *v['vertices']), 2),
              put(struct.pack(f">{len(v['records'])}H", *(r << 2 | k for r, k in v['records'])), 2),
              put(b''.join(struct.pack('>3h', *p) for c in coords for p in c), 2),
              put(b''.join(struct.pack('>3h', *p) for c in positions for p in c), 2)]
        rows += struct.pack('>BBHHH4I', v['channel'], blocks[v['part'], v['row']], len(v['vertices']),
                            len(v['records']), present, *at)
    return rows, len(variants)


def light_rigs(exe, stages=20):
    """Each stage's fighter light rig from its 28-byte record (FUN_80039BB4; documented in
    Tekken3Remake docs/research/code/stages.md, Lighting): ambient word, base RGB, main light
    RGB and the main light's matrix row, the negated unit direction in Q12."""
    rows = []
    for stage in range(stages):
        at = usa_file_offset(STAGE_RECORDS + STAGE_RECORD_BYTES * stage)
        ambient = struct.unpack_from('<i', exe, at)[0]
        base, light = exe[at + 4:at + 7], exe[at + 8:at + 11][::-1]
        pitch, yaw = (a * math.tau / 4096 for a in struct.unpack_from('<hh', exe, at + 12))
        direction = (math.cos(yaw - math.pi) * math.cos(pitch), math.sin(pitch),
                     math.sin(yaw - math.pi) * math.cos(pitch))
        rows.append((ambient, base[0] | base[1] << 8 | base[2] << 16, light[0] | light[1] << 8 | light[2] << 16,
                     *(round(-4096 * c) for c in direction)))
    return rows


# FUN_8003A6E4's Q12 tables: sine (0x8001E8C4) and cosine, a quarter turn on; index & 0xFFF.
SINE_TABLE, COSINE_TABLE = 0x8001E8C4, 0x8001F0C4
STAGE_SHADOW_ANGLES = 0x14  # b, a: FUN_8003A098


def shadow_shear(read16, stage):
    """The stage's floor shear (m01, m21) at 0x800B0018 (FUN_80036180; Tekken3Remake
    docs/research/code/stages.md, Lighting): the unit x axis turned through FUN_8003A6E4 by
    (0, -a, -b), then m01 = -4096*dx/dy and m21 = -4096*dz/dy (C division, s16 results).
    read16(address) is a signed 16-bit read of a Rev.1 address."""
    b, a = (read16(STAGE_RECORDS + STAGE_RECORD_BYTES * stage + STAGE_SHADOW_ANGLES + 2 * k) for k in (0, 1))
    angle = (0, -a, -b)
    s = [read16(SINE_TABLE + 2 * (v & 0xFFF)) for v in angle]
    c = [read16(COSINE_TABLE + 2 * (v & 0xFFF)) for v in angle]
    # FUN_8003A6E4's first column, with the GTE's IR0 * IR >> 12 products.
    cxcz, cxsz = c[0] * c[2] >> 12, c[0] * s[2] >> 12
    dx = c[1] * c[2] >> 12
    dy = ((s[1] * (s[0] * c[2] >> 12) >> 12) + cxsz)
    dz = ((s[0] * s[2] >> 12) - (s[1] * cxcz >> 12))
    dx, dy, dz = (((v + 0x8000) & 0xFFFF) - 0x8000 for v in (dx, dy, dz))
    if not dy:
        raise ValueError(f'stage {stage}: the shadow light is level')

    def quotient(v):
        q = abs(v << 12) // abs(dy)
        q = q if (v < 0) == (dy < 0) else -q
        return ((-q + 0x8000) & 0xFFFF) - 0x8000
    return quotient(dx), quotient(dz)


def shadow_shears(exe, stages=20):
    return [shadow_shear(lambda address: struct.unpack_from('<h', exe, usa_file_offset(address))[0], stage)
            for stage in range(stages)]


# TK3psSDW, ARC member 3 (Tekken3Remake docs/research/formats/pssdw-member.md).
SHADOW_MAGIC, SHADOW_SECTIONS = b'TK3psSDW', 18


def shadow_mesh(arc):
    """The drop-shadow mesh as the pack's shadow block, or (b'', 0) without one: the u16 slot
    count, the u16 first slot of each of the 18 joint sections and the end, s16 (x, y, z) per
    slot (padding slots kept: the indices count them), then four slot bytes per face in VDP1
    perimeter order (a triangle repeats its last corner; a POLY_F4 row's 0, 1, 2, 3 is 0, 1, 3, 2)."""
    data = arc_member(arc, 3)
    if not data:
        return b'', 0
    if data[:8] != SHADOW_MAGIC:
        raise ValueError('ARC member 3 is not TK3psSDW')
    end, word_count, row_count = struct.unpack_from('<3I', data, 8)
    offsets = struct.unpack_from(f'<{SHADOW_SECTIONS}I', data, 0x14)
    coords, firsts = [], []
    for i, at in enumerate(offsets):
        firsts.append(len(coords))
        slots = -(-struct.unpack_from('<I', data, at)[0] // 3) * 3
        if at + 4 + 8 * slots != (offsets[i + 1] if i + 1 < SHADOW_SECTIONS else end):
            raise ValueError(f'shadow section {i} does not end at the next one')
        coords += [struct.unpack_from('<3h', data, at + 4 + 8 * k) for k in range(slots)]
    firsts.append(len(coords))
    if struct.unpack_from('<I', data, end)[0] != word_count:
        raise ValueError('shadow triangle count disagrees')
    words = struct.unpack_from(f'<{word_count}I', data, end + 4)
    at = end + 8 + 4 * word_count
    if struct.unpack_from('<I', data, at - 4)[0] != row_count or at + 4 * row_count != len(data):
        raise ValueError('shadow quad rows disagree')
    faces = [(w >> 2 & 0xFF, w >> 10 & 0xFF, w >> 20 & 0xFF, w >> 20 & 0xFF) for w in words]
    faces += [(r[0], r[1], r[3], r[2]) for r in (data[at + 4 * k:at + 4 * k + 4] for k in range(row_count))]
    if len(coords) > 256 or any(v >= len(coords) for f in faces for v in f):
        raise ValueError('shadow face index out of range')
    block = struct.pack(f'>{2 + SHADOW_SECTIONS}H', len(coords), *firsts)
    block += b''.join(struct.pack('>3h', *v) for v in coords) + bytes(v for f in faces for v in f)
    return block, len(faces)


# FUN_800364E8 / FUN_80036518: a byte per costume slot, the hand shape while attacking (and the
# costume's own) and on a clean hit (Tekken3Remake tools/remake_import/tables.py; NOTICE.md).
FACE_ATTACK, FACE_HIT, FACE_SLOTS = 0x80095D74, 0x80095DA8, 0x37


def face_shapes(exe):
    """(attack, hit) hand shapes per character id, from its costume slot."""
    tables = [exe[usa_file_offset(at):][:FACE_SLOTS] for at in (FACE_ATTACK, FACE_HIT)]
    by_id = {f.id: f for f in ROSTER}
    out = []
    for character in range(max(by_id) + 1):
        slot = by_id[character].slot
        out.append(tuple(t[slot] if slot < FACE_SLOTS else 0 for t in tables))
        if any(v >= 8 for v in out[-1]):
            raise ValueError(f'character {character}: hand shape {out[-1]} beyond the hand shapes')
    return out


def compact_bytes(archive):
    """sat_character_asset_compact's result for this archive, for sizing storage."""
    head = struct.unpack_from('>24I', archive)
    at, count = head[SKA_FIELD_TEXTURE_OFFSET], head[SKA_FIELD_TEXTURES]
    first, last = len(archive), 0
    for i in range(count):
        payload, _, _, flags, _, pixels = struct.unpack_from('>IHHHHI', archive, at + SKA_TEXTURE_BYTES * i)
        size = pixels // 2 if flags & 0x8000 else pixels
        first, last = min(first, payload), max(last, payload + size)
    return ((first + 3) & ~3) - count * SKA_TEXTURE_BYTES + len(archive) - ((last + 3) & ~3)


def logic_bank(raw_path, scratch):
    subprocess.run([sys.executable, str(SCRIPTS / 'divmot_to_logic.py'), '--bank', str(raw_path),
                    '--output', str(scratch), '--with-motion', '--with-hitboxes', '--with-events',
                    '--with-sounds'], check=True, stdout=subprocess.DEVNULL)
    found = sorted(scratch.glob('bank_*.t3logic'))
    if len(found) != 1:
        raise ValueError(f'expected one logic bank from {raw_path}')
    return found[0].read_bytes()


def build(fighter, raw, exe, system_vram, libsaturn, scratch, texture_cap=0, hand_variants=False):
    r = records(fighter)
    arc = (raw / f"{r['arc']:03}.bin").read_bytes()
    model = parse_kmd((raw / f"{r['kmd']:03}.bin").read_bytes())
    archive, report = compile_mesh(model, libsaturn, scratch / 'mesh', raw / f"{r['arc']:03}.bin", texture_cap)
    if report['joints'] != 22 or not report['original_textures']:
        raise ValueError(f'{fighter.name}: unexpected rig or texture kind')
    raw_bank = (raw / f'{bank_record(fighter):03}.bin').read_bytes()
    bank = logic_bank(raw / f'{bank_record(fighter):03}.bin', scratch / 'logic')
    choices, choice_count, streams, stream_count, encoded = camera_data(raw_bank)
    table, pcm, voice_count = voices(exe, fighter, (raw / f"{r['vab']:03}.bin").read_bytes(), arc)

    # The hot block: read once into the actor's High WRAM storage and kept for the match.
    hot, fields = bytearray(), {}

    def place(name, data, boundary=4):
        align(hot, boundary)
        fields[name] = len(hot)
        hot.extend(data)

    place('parts', b''.join(struct.pack('>bBxxiii', parent, slot, x, y, z)
                            for parent, slot, (x, y, z) in skeleton_parts(model)))
    place('radii', struct.pack('>8h', *body_radii(fighter)))
    place('choices', choices)
    place('streams', streams)
    place('double_sided', bytes(report['double_sided_faces']))
    mesh, faces, projected_vertices = projected_mesh(model, report)

    def put(data, boundary):
        align(hot, boundary)
        at = len(hot)
        hot.extend(data)
        return at

    projected_rows = projected_table(mesh, put)
    place('projected', projected_rows)
    place('projected_faces', b''.join(struct.pack('>4H', *f) for f in faces), 2)
    place('face_ot', bytes(v for row in report['face_ot_metadata'] for v in row), 1)
    # Lighting needs LUT4 textures: VDP1 Gouraud does not shade INDEX8 texels.
    lighting = report['texture_report'].get('lighting')
    normal_count = 0
    for name in ('light_normals', 'light_faces', 'light_corners', 'light_tones'):
        fields[name] = 0
    if lighting:
        normals, light_faces, corners, tones, normal_count = lighting_data(lighting)
        place('light_normals', normals)
        place('light_faces', light_faces, 2)
        place('light_corners', corners, 2)
        place('light_tones', tones, 2)
    hand_rows, hand_row_count = hand_variant_rows(report['hand_variants'], mesh, put) if hand_variants else (b'', 0)
    if hand_rows:
        place('hand_variants', hand_rows)
    else:
        fields['hand_variants'] = 0
    shadow, shadow_faces = shadow_mesh(arc)
    fields['shadow'] = 0
    if shadow:
        place('shadow', shadow, 2)
    align(hot, 4)

    sections = dict(hot=bytes(hot), archive=archive, bank=bank, camera=encoded,
                    flipbook=flipbook_file(arc)[0], voices=table, pcm=pcm,
                    names=names(arc, fighter.name_width, system_vram))
    image = bytearray(HEADER_BYTES)
    directory = []
    for name in SECTIONS:
        align(image, 16)
        directory += [len(image), len(sections[name])]
        image += sections[name]
    palettes = struct.unpack_from('>I', archive, 4 * SKA_FIELD_PALETTES)[0]
    words = [MAGIC, VERSION, fighter.id, bank[1], fighter.voice_set, model.scale_percent, fighter.name_width,
             report['compiled_vertices'], report['faces'], report['textures'], palettes,
             fields['parts'], fields['radii'], fields['choices'], choice_count, fields['streams'], stream_count,
             fields['double_sided'], voice_count, compact_bytes(archive),
             fnv1a(bank), fields['projected'], len(mesh), fields['projected_faces'], projected_vertices,
             fields['face_ot'], *directory, normal_count, fields['light_normals'], fields['light_faces'],
             fields['light_corners'], fields['light_tones'], hand_row_count, fields['hand_variants'],
             shadow_faces, fields['shadow']]
    if len(words) * 4 > HEADER_BYTES:
        raise ValueError('pack header overflow')
    struct.pack_into(f'>{len(words)}I', image, 0, *words)
    return bytes(image), bank, dict(
        id=fighter.id, name=fighter.name, file=f'F{fighter.id:02}.BIN', bytes=len(image), bank_type=bank[1],
        bank_bytes=len(bank), bank_record=bank_record(fighter), branch_rows=branch_rows(bank),
        vertices=report['compiled_vertices'], projected_vertices=projected_vertices,
        faces=report['faces'], textures=report['textures'],
        palettes=palettes, scale=model.scale_percent, hot_bytes=len(hot), archive_bytes=len(archive),
        compact_bytes=compact_bytes(archive), camera_bytes=len(encoded), choices=choice_count,
        streams=stream_count, voices=voice_count, pcm_bytes=len(pcm),
        max_samples=max((struct.unpack_from('>I', table, i + 8)[0] for i in range(0, len(table), VOICE_ROW_BYTES)), default=0))


def branch_rows(bank):
    """Rows of a logic bank's branch section (sections 0 and 1 of the header at +4)."""
    first, second = struct.unpack_from('<II', bank, 4)
    return (second - first) // 12


def header(rows, common_rows, homes, rigs, faces, shears):
    def most(key):
        return max(row[key] for row in rows)
    pairs = [(a, b) for a in rows for b in rows]
    arena = max((a['bank_bytes'] + 15 & ~15) + (0 if a['bank_type'] == b['bank_type'] else (b['bank_bytes'] + 15 & ~15)) +
                (a['camera_bytes'] + 15 & ~15) + (b['camera_bytes'] + 15 & ~15) for a, b in pairs)
    # Each actor's store starts aligned, so the size is a multiple of 16.
    store = (max(row['hot_bytes'] + row['compact_bytes'] for row in rows) + 16 + 15) & ~15
    filters = filter_words(common_rows) + max(
        filter_words(a['branch_rows']) + (0 if a['bank_type'] == b['bank_type'] else filter_words(b['branch_rows']))
        for a, b in pairs)
    lines = ['#ifndef T3_ROSTER_H', '#define T3_ROSTER_H', '/* Generated by build_fighter_packs.py. */',
             f'#define T3_ROSTER_COUNT {len(rows)}u',
             f"#define T3_ROSTER_VERTICES {most('vertices')}u",
             f"#define T3_ROSTER_PROJECTED_VERTICES {most('projected_vertices')}u",
             f"#define T3_ROSTER_FACES {most('faces')}u",
             f"#define T3_ROSTER_TEXTURES {most('textures')}u",
             f"#define T3_ROSTER_VOICES {most('voices')}u",
             f"#define T3_ROSTER_ARCHIVE_BYTES {most('archive_bytes')}u",
             f"#define T3_ROSTER_MAX_VOICE_SAMPLES {most('max_samples')}u",
             f'#define T3_ROSTER_ARENA_BYTES {arena}u',
             f'#define T3_ROSTER_STORE_BYTES {store}u',
             f'#define T3_ROSTER_FILTER_WORDS {filters}u',
             '/* Home stage and music track, by character id. */',
             f"#define T3_ROSTER_STAGES {{{','.join(str(home[0]) for home in homes)}}}",
             f"#define T3_ROSTER_TRACKS {{{','.join(str(home[1]) for home in homes)}}}",
             '/* Per stage: ambient, base RGB, light RGB (R in the low byte), light row x, y, z (Q12). */',
             f'#define T3_ROSTER_LIGHT_RIG_COUNT {len(rigs)}u',
             '#define T3_ROSTER_LIGHT_RIGS {' + ','.join('{' + ','.join(str(v) for v in rig) + '}' for rig in rigs) + '}',
             '/* Hand shapes by character id: attacking (and the costume\'s own), on a clean hit. */',
             '#define T3_ROSTER_FACE_SHAPES {' + ','.join('{%d,%d}' % face for face in faces) + '}',
             '/* Per stage: the floor shear m01, m21 (Q12) that flattens the shadow meshes. */',
             '#define T3_ROSTER_SHADOW_SHEARS {' + ','.join('{%d,%d}' % s for s in shears) + '}',
             '#endif', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True, help='private directory of NNN.bin records')
    parser.add_argument('--exe', type=Path, required=True, help='USA SLUS_004.02')
    parser.add_argument('--libsaturn-source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--only', type=int, action='append', help='build only these character ids (a test aid)')
    parser.add_argument('--texture-cap', type=int, default=0,
                        help='0: 8x8 INDEX8 face patches; N: native-density LUT4 patches of at most N texels a side')
    parser.add_argument('--hand-variants', action='store_true',
                        help='store the hand and jaw vertex variants (T3_FIGHTER_HAND_VARIANTS)')
    args = parser.parse_args()
    exe = args.exe.read_bytes()
    system_vram = system_textures.build_vram(system_textures.members(exe, 'usa'))
    args.out.mkdir(parents=True, exist_ok=True)
    banks = args.out / 'banks'
    banks.mkdir(exist_ok=True)
    rows = []
    try:
        for fighter in ROSTER:
            if args.only and fighter.id not in args.only:
                continue
            with tempfile.TemporaryDirectory() as scratch:
                image, bank, row = build(fighter, args.raw, exe, system_vram, args.libsaturn_source, Path(scratch),
                                         args.texture_cap, args.hand_variants)
            (args.out / row['file']).write_bytes(image)
            (banks / f"{fighter.id:02}.t3logic").write_bytes(bank)
            rows.append(row)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'fighter pack failed: {error}\n')
    with tempfile.TemporaryDirectory() as scratch:
        common = logic_bank(args.raw / f'{COMMON_RECORD:03}.bin', Path(scratch) / 'logic')
    homes = [fighter_home(exe, character) for character in range(max(f.id for f in ROSTER) + 1)]
    if {f.id for f in ROSTER} != set(range(len(homes))) or not {home[0] for home in homes} <= set(STAGE_NUMBERS):
        raise ValueError(f'a character has no baked home stage: {homes}')
    (args.out / 't3_roster.h').write_text(header(rows, branch_rows(common), homes, light_rigs(exe), face_shapes(exe),
                                                    shadow_shears(exe)))
    (args.out / 'roster.json').write_text(json.dumps(rows, indent=1) + '\n')
    print(json.dumps(dict(packs=len(rows), bytes=sum(r['bytes'] for r in rows)), indent=1))


if __name__ == '__main__':
    main()
