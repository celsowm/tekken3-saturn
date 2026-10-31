#!/usr/bin/env python3
"""Private local KMD -> LibSaturn SKA1 -> embedded C for a visual bind-pose test.

The output embeds commercial model geometry and MUST stay only in gitignored
build directories. This generator is NOT called in GitHub Actions with a real KMD.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from kmd_to_ska import parse_kmd, compile_ska, PART_ROWS, PART_MATRICES, SECOND_MESH
from compiled_render_plan import emit_c

def skeleton_parts(model):
    """Return the original loader's draw order and Z fixup, without mesh data."""
    result=[]
    for part,(row_i,slot) in enumerate(zip(PART_ROWS,PART_MATRICES)):
        row=model.rows[row_i]
        if row.parent<-1 or row.parent>=part:
            raise ValueError(f"bad KMD draw-part parent {part}: {row.parent}")
        x,y,z=row.offset
        result.append((row.parent,slot,(x,y,-z)))
    return result

def compile_mesh(model,lib_source,preview,texture_arc):
    """One KMD render mesh -> (SKA1 archive, compile report), budget-checked."""
    report = compile_ska(model,lib_source,preview,texture_arc)
    archive = (preview/"fighter_bind.ska").read_bytes()
    vertices = report["compiled_vertices"]
    faces = report["faces"]
    if vertices < 3 or vertices > 1800 or faces < 1 or faces > 900:
        raise ValueError(f"local KMD exceeds experimental render budget: {vertices} vertices, {faces} faces")
    if len(archive)>350000:
        raise ValueError(f"local KMD SKA1 exceeds the 350-KiB test memory budget: {len(archive)}")
    return archive,report

def c_rows(values,width):
    return ['    '+','.join(str(v) for v in values[i:i+width])+',' for i in range(0,len(values),width)]

def projected_mesh(model, report):
    """The original vertex blocks in draw order, the logical faces and their vertex count."""
    mapping = {}
    logical_indices = {}
    for logical, (part, row, slot, index) in enumerate(report['projected_vertex_map']):
        if logical >= 65536:
            raise ValueError('projected logical vertex index exceeds uint16')
        mapping.setdefault((part, row), []).append((slot, index, logical))
        logical_indices[part, row, slot] = logical
    blocks = []
    for part, row_index in enumerate(PART_ROWS[:18]):
        for ri in [row_index] + ([row_index + 1] if part in SECOND_MESH else []):
            vb = model.rows[ri].vert
            if vb is None:
                continue
            entries = mapping.get((part, ri), [])
            blocks.append(dict(part=part, row=ri, vert=vb, entries=entries,
                               map=[x for slot, index, _ in entries for x in (slot, index)],
                               logical=[x for slot, _, index in entries for x in (slot, index)]))
    faces = []
    for part, row_index in enumerate(PART_ROWS[:18]):
        for ri in [row_index] + ([row_index + 1] if part in SECOND_MESH else []):
            for face in model.rows[ri].faces or []:
                v = [logical_indices[part, ri, slot] for slot in face]
                faces.append((v[0], v[1], v[2], v[2]) if len(v) == 3 else (v[0], v[1], v[3], v[2]))
    if len(faces) != report['faces']:
        raise ValueError('projected face count differs from compiled mesh')
    return blocks, faces, len(logical_indices)

def emit_projected_mesh(model, report, suffix):
    """Emit private original vertex blocks and compiled-index mappings."""
    name = 't3_kmd_projected' + suffix
    lines = []
    blocks = []
    mesh, faces, vertex_count = projected_mesh(model, report)
    for block in mesh:
            part, ri, vb = block['part'], block['row'], block['vert']
            prefix = f'{name}_{part}_{ri}'
            arrays = [('local', 'uint8_t', vb.imports_local),
                      ('global', 'uint8_t', vb.imports_global)]
            arrays += [(f'post{i}', 'uint16_t', values) for i, values in enumerate(vb.posts)]
            arrays += [('map', 'uint16_t', block['map']), ('logical', 'uint16_t', block['logical'])]
            for label, typ, values in arrays:
                if values:
                    lines.append(f'static const {typ} {prefix}_{label}[] = {{' + ','.join(map(str, values)) + '};')
            if vb.coords:
                lines.append(f'static const int16_t {prefix}_xyz[][3] = {{' + ','.join('{%d,%d,%d}' % v for v in vb.coords) + '};')
            def ref(label, values):
                return f'{prefix}_{label}' if values else 'NULL'
            posts = ','.join(ref(f'post{i}', values) for i, values in enumerate(vb.posts))
            counts = ','.join(str(len(values)) for values in vb.posts)
            map_values = block['entries']
            blocks.append('    {%d,%s,%d,%s,%d,%s,%d,{%s},{%s},%s,%s,%d},' % (
                part, ref('local', vb.imports_local), len(vb.imports_local),
                ref('global', vb.imports_global), len(vb.imports_global),
                ref('xyz', vb.coords), len(vb.coords), posts, counts,
                ref('map', map_values), ref('logical', map_values), len(map_values)))
    lines.append(f'const t3_psx_projected_part_t {name}[] = {{')
    lines.extend(blocks)
    lines.append('};')
    lines.append(f'const unsigned {name}_count = {len(blocks)};')
    lines.append(f'const uint16_t {name}_faces[][4] = {{' + ','.join('{%d,%d,%d,%d}' % f for f in faces) + '};')
    lines.append(f'const unsigned {name}_vertex_count = {vertex_count};')
    return lines

def generate_roster(output):
    """The fixed-buffer sizes of a build that loads its fighters from packs
    (scripts/build_fighter_packs.py): no mesh is embedded, t3_roster.h holds the counts."""
    output.mkdir(parents=True,exist_ok=True)
    (output/"t3_generated_character.h").write_text("""#ifndef T3_GENERATED_CHARACTER_H
#define T3_GENERATED_CHARACTER_H
#include <stdint.h>
#include "psx_pose.h"
#include "t3_roster.h"
#define T3_SECOND_MESH 1
#define T3_ARCHIVE_BYTES T3_ROSTER_ARCHIVE_BYTES
#define T3_VERTEX_COUNT T3_ROSTER_VERTICES
#define T3_PROJECTED_VERTEX_COUNT T3_ROSTER_PROJECTED_VERTICES
#define T3_FACE_COUNT T3_ROSTER_FACES
#define T3_JOINT_COUNT 22u
#define T3_TEXTURE_COUNT T3_ROSTER_TEXTURES
#define T3_ORIGINAL_TEXTURES 1
#define T3_CLIP_COUNT 1u
#define T3_CLIP_IDLE 0u
#define T3_LOCAL_KMD_BIND 1u
#define T3_KMD_SCALE_PERCENT 100u
#endif
""",encoding='utf8')
    (output/"t3_generated_character.c").write_text('#include "t3_generated_character.h"\n',encoding='utf8')
    (output/"manifest.json").write_text(json.dumps(dict(fighter_packs=True),indent=2)+'\n',encoding='utf8')

def generate(kmd_path, lib_source, output,texture_arc=None,actor0_kmd=None,actor1_kmd=None,
             actor1_mesh=None,actor1_arc=None):
    raw = kmd_path.read_bytes()
    model = parse_kmd(raw)
    mesh1_raw=actor1_mesh.read_bytes() if actor1_mesh is not None else None
    mesh1_model=parse_kmd(mesh1_raw) if mesh1_raw is not None else None
    actor0_raw=actor0_kmd.read_bytes() if actor0_kmd is not None else None
    actor0_model=parse_kmd(actor0_raw) if actor0_raw is not None else model
    # P2's own render mesh supplies its default skeleton.
    if actor1_kmd is None and mesh1_raw is not None:
        actor1_raw,actor1_model=mesh1_raw,mesh1_model
    else:
        actor1_raw=actor1_kmd.read_bytes() if actor1_kmd is not None else None
        actor1_model=parse_kmd(actor1_raw) if actor1_raw is not None else model
    if actor0_model.scale_percent!=model.scale_percent or actor1_model.scale_percent!=model.scale_percent:
        raise ValueError('actor skeletons must use the same scale')
    archive,report = compile_mesh(model,lib_source,output/"kmd_preview",texture_arc)
    vertices,faces,joints,textures = (report["compiled_vertices"],report["faces"],
                                      report["joints"],report["textures"])
    archive1=report1=None
    if mesh1_model is not None:
        if mesh1_model.scale_percent!=model.scale_percent:
            raise ValueError('actor meshes must use the same scale')
        archive1,report1 = compile_mesh(mesh1_model,lib_source,output/"kmd_preview1",actor1_arc)
        if report1["joints"]!=joints or report1["original_textures"]!=report["original_textures"]:
            raise ValueError('actor meshes must share the joint count and texture kind')
        # Shared buffers take the larger of the two meshes.
        vertices=max(vertices,report1["compiled_vertices"])
        faces=max(faces,report1["faces"])
        textures=max(textures,report1["textures"])
    output.mkdir(parents=True,exist_ok=True)
    second_header='' if archive1 is None else f"""#define T3_SECOND_MESH 1
#define T3_ARCHIVE1_BYTES {len(archive1)}u
typedef union t3_embedded_archive1 {{
    uint32_t alignment;
    uint8_t bytes[T3_ARCHIVE1_BYTES];
}} t3_embedded_archive1_t;
extern t3_embedded_archive1_t t3_archive1;
extern const uint8_t t3_kmd_double_sided1[T3_FACE_COUNT];
#if defined(T3_PARTICLE_FIGHTER_OT)
extern const uint8_t t3_kmd_face_ot1[T3_FACE_COUNT][2];
#endif
"""
    palette=[0,0xBDEF]+[0]*254
    alt_palette=[0,0x801F]+[0]*254
    header=f"""#ifndef T3_GENERATED_CHARACTER_H
#define T3_GENERATED_CHARACTER_H
#include <stdint.h>
#include "psx_pose.h"
#if defined(T3_PARTICLE_FIGHTER_OT)
#include "psx_projected_mesh.h"
extern const t3_psx_projected_part_t t3_kmd_projected[];
extern const unsigned t3_kmd_projected_count;
extern const uint16_t t3_kmd_projected_faces[][4];
extern const unsigned t3_kmd_projected_vertex_count;
extern const t3_psx_projected_part_t t3_kmd_projected1[];
extern const unsigned t3_kmd_projected1_count;
extern const uint16_t t3_kmd_projected1_faces[][4];
extern const unsigned t3_kmd_projected1_vertex_count;
#endif
#define T3_ARCHIVE_BYTES {len(archive)}u
/* With a second mesh these are the larger of the two; each actor's own
 * counts come from its decoded asset. */
#define T3_VERTEX_COUNT {vertices}u
#define T3_PROJECTED_VERTEX_COUNT {max(report['logical_vertices'], report1['logical_vertices'] if report1 else 0)}u
#define T3_FACE_COUNT {faces}u
#define T3_JOINT_COUNT {joints}u
#define T3_TEXTURE_COUNT {textures}u
#define T3_ORIGINAL_TEXTURES {int(report['original_textures'])}
#define T3_CLIP_COUNT 1u
#define T3_CLIP_IDLE 0u
#define T3_LOCAL_KMD_BIND 1u
#define T3_KMD_SCALE_PERCENT {model.scale_percent}u
typedef union t3_embedded_archive {{
    uint32_t alignment;
    uint8_t bytes[T3_ARCHIVE_BYTES];
}} t3_embedded_archive_t;
extern t3_embedded_archive_t t3_archive;
{second_header}extern const uint16_t t3_red_palette[256];
extern const uint8_t t3_kmd_double_sided[T3_FACE_COUNT];
#if defined(T3_PARTICLE_FIGHTER_OT)
extern const uint8_t t3_kmd_face_ot[T3_FACE_COUNT][2];
#endif
extern const t3_psx_part_t t3_private_kmd_parts[T3_PSX_DRAW_PARTS];
extern const t3_psx_part_t* const t3_private_actor_parts[2];
#endif
"""
    payload=[
        '#include "t3_generated_character.h"',
        '/* Contains the user-supplied copyrighted KMD geometry. PRIVATE BUILD ONLY. */',
        't3_embedded_archive_t t3_archive = { .bytes = {'
    ]
    payload.extend(c_rows(archive,24))
    payload.append('} };')
    if archive1 is not None:
        payload.append('t3_embedded_archive1_t t3_archive1 = { .bytes = {')
        payload.extend(c_rows(archive1,24))
        payload.append('} };')
        payload.append('const uint8_t t3_kmd_double_sided1[T3_FACE_COUNT] = {')
        payload.extend(c_rows(report1['double_sided_faces'],32))
        payload.append('};')
        payload.append('#if defined(T3_PARTICLE_FIGHTER_OT)')
        payload.append('const uint8_t t3_kmd_face_ot1[T3_FACE_COUNT][2] = {')
        payload.extend('    {%d,%d},' % tuple(row) for row in report1['face_ot_metadata'])
        payload.append('};')
        payload.append('#endif')
    payload.append('const uint16_t t3_red_palette[256] = {')
    for i in range(0,256,16):
        payload.append('    '+','.join(str(v) for v in alt_palette[i:i+16])+',')
    payload.append('};')
    payload.append('const uint8_t t3_kmd_double_sided[T3_FACE_COUNT] = {')
    flags=report['double_sided_faces']
    for i in range(0,len(flags),32):
        payload.append('    '+','.join(str(v) for v in flags[i:i+32])+',')
    payload.append('};')
    payload.append('#if defined(T3_PARTICLE_FIGHTER_OT)')
    payload.append('const uint8_t t3_kmd_face_ot[T3_FACE_COUNT][2] = {')
    payload.extend('    {%d,%d},' % tuple(row) for row in report['face_ot_metadata'])
    payload.append('};')
    payload.append('#endif')
    payload.append('#if defined(T3_PARTICLE_FIGHTER_OT)')
    payload.extend(emit_projected_mesh(model, report, ''))
    if mesh1_model is not None:
        payload.extend(emit_projected_mesh(mesh1_model, report1, '1'))
    payload.append('#endif')
    # These ORIGINAL draw-part parent indices and model offsets are private,
    # and must be interpreted in PS1 coordinate space, never guessed from GLB.
    payload.append('const t3_psx_part_t t3_private_kmd_parts[T3_PSX_DRAW_PARTS] = {')
    for parent,slot,(x,y,z) in skeleton_parts(model):
        payload.append('    {%d,%du,{%d,%d,%d}},' % (parent,slot,x,y,z))
    payload.append('};')
    if actor0_raw is not None:
        payload.append('static const t3_psx_part_t actor0_parts[T3_PSX_DRAW_PARTS] = {')
        for parent,slot,(x,y,z) in skeleton_parts(actor0_model):
            payload.append('    {%d,%du,{%d,%d,%d}},' % (parent,slot,x,y,z))
        payload.append('};')
    if actor1_raw is not None:
        payload.append('static const t3_psx_part_t actor1_parts[T3_PSX_DRAW_PARTS] = {')
        for parent,slot,(x,y,z) in skeleton_parts(actor1_model):
            payload.append('    {%d,%du,{%d,%d,%d}},' % (parent,slot,x,y,z))
        payload.append('};')
    payload.append('const t3_psx_part_t* const t3_private_actor_parts[2] = {%s,%s};' %
                   ('actor0_parts' if actor0_raw is not None else 't3_private_kmd_parts',
                    'actor1_parts' if actor1_raw is not None else 't3_private_kmd_parts'))
    payload.extend(emit_c(json.loads((output/"kmd_preview"/"render_plan.json").read_text())))
    (output/"t3_generated_character.c").write_text('\n'.join(payload)+'\n',encoding='utf8')
    (output/"t3_generated_character.h").write_text(header,encoding='utf8')
    report.update(
        input_sha256=hashlib.sha256(raw).hexdigest(),
        actor0_skeleton_sha256=hashlib.sha256(actor0_raw or raw).hexdigest(),
        actor1_skeleton_sha256=hashlib.sha256(actor1_raw or raw).hexdigest(),
        shared_render_mesh=archive1 is None,
        scale_percent=model.scale_percent,
        local_kmd_bind_preview=True,
        distribution="PRIVATE, no commercial game data may enter Git or GitHub Actions")
    if report1 is not None:
        report.update(actor1_mesh_sha256=hashlib.sha256(mesh1_raw).hexdigest(),
                      actor1_mesh={k:v for k,v in report1.items() if k not in ('double_sided_faces','face_ot_metadata','projected_vertex_map')},
                      max_vertices=vertices,max_faces=faces,max_textures=textures)
    (output/"manifest.json").write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='utf8')
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kmd",type=Path)
    p.add_argument("--libsaturn-source",type=Path)
    p.add_argument("--fighter-packs",action="store_true",help="embed no mesh: t3_roster.h sizes the buffers")
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--textures-arc",type=Path,help="private matching character ARC textures")
    p.add_argument("--actor0-kmd",type=Path,help="private P1 skeleton override; render mesh remains --kmd")
    p.add_argument("--actor1-kmd",type=Path,help="private P2 skeleton override; render mesh remains --kmd")
    p.add_argument("--actor1-mesh-kmd",type=Path,help="private P2 render mesh (a second character)")
    p.add_argument("--actor1-textures-arc",type=Path,help="private texture ARC matching --actor1-mesh-kmd")
    a=p.parse_args()
    if a.fighter_packs:
        generate_roster(a.output)
        return
    if a.kmd is None or a.libsaturn_source is None:
        p.error("--kmd and --libsaturn-source are required without --fighter-packs")
    if a.actor1_textures_arc is not None and a.actor1_mesh_kmd is None:
        p.error("--actor1-textures-arc requires --actor1-mesh-kmd")
    try:
        result=generate(a.kmd,a.libsaturn_source,a.output,a.textures_arc,a.actor0_kmd,a.actor1_kmd,
                        a.actor1_mesh_kmd,a.actor1_textures_arc)
    except (OSError,ValueError,ImportError,AssertionError) as exc:
        p.exit(1,f"local KMD embed failed: {exc}\n")
    print(json.dumps({k:v for k,v in result.items() if k not in ('double_sided_faces','face_ot_metadata','projected_vertex_map')},sort_keys=True))

if __name__=="__main__":main()
