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
from fighter_roster import COMMON_RECORD, ROSTER, bank_record, body_radii, filter_words, records
from kmd_to_ska import parse_kmd
from psx_character_textures import load_character_vram

SCRIPTS = Path(__file__).resolve().parent
MAGIC = 0x54334650
VERSION = 3
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


def build(fighter, raw, exe, system_vram, libsaturn, scratch):
    r = records(fighter)
    arc = (raw / f"{r['arc']:03}.bin").read_bytes()
    model = parse_kmd((raw / f"{r['kmd']:03}.bin").read_bytes())
    archive, report = compile_mesh(model, libsaturn, scratch / 'mesh', raw / f"{r['arc']:03}.bin")
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
             fields['face_ot'], *directory]
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


def header(rows, common_rows):
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
             '#endif', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True, help='private directory of NNN.bin records')
    parser.add_argument('--exe', type=Path, required=True, help='USA SLUS_004.02')
    parser.add_argument('--libsaturn-source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--only', type=int, action='append', help='build only these character ids (a test aid)')
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
                image, bank, row = build(fighter, args.raw, exe, system_vram, args.libsaturn_source, Path(scratch))
            (args.out / row['file']).write_bytes(image)
            (banks / f"{fighter.id:02}.t3logic").write_bytes(bank)
            rows.append(row)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'fighter pack failed: {error}\n')
    with tempfile.TemporaryDirectory() as scratch:
        common = logic_bank(args.raw / f'{COMMON_RECORD:03}.bin', Path(scratch) / 'logic')
    (args.out / 't3_roster.h').write_text(header(rows, branch_rows(common)))
    (args.out / 'roster.json').write_text(json.dumps(rows, indent=1) + '\n')
    print(json.dumps(dict(packs=len(rows), bytes=sum(r['bytes'] for r in rows)), indent=1))


if __name__ == '__main__':
    main()
