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

from kmd_to_ska import parse_kmd, compile_ska, PART_ROWS, PART_MATRICES
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

def generate(kmd_path, lib_source, output,texture_arc=None,actor0_kmd=None):
    raw = kmd_path.read_bytes()
    model = parse_kmd(raw)
    actor0_raw=actor0_kmd.read_bytes() if actor0_kmd is not None else None
    actor0_model=parse_kmd(actor0_raw) if actor0_raw is not None else model
    if actor0_model.scale_percent!=model.scale_percent:
        raise ValueError('actor skeletons must use the same scale')
    preview = output/"kmd_preview"
    report = compile_ska(model,lib_source,preview,texture_arc)
    archive = (preview/"fighter_bind.ska").read_bytes()
    vertices = report["compiled_vertices"]
    faces = report["faces"]
    joints = report["joints"]
    if vertices < 3 or vertices > 1800 or faces < 1 or faces > 900:
        raise ValueError(f"local KMD exceeds experimental render budget: {vertices} vertices, {faces} faces")
    if len(archive)>350000:
        raise ValueError(f"local KMD SKA1 exceeds the 350-KiB test memory budget: {len(archive)}")
    output.mkdir(parents=True,exist_ok=True)
    palette=[0,0xBDEF]+[0]*254
    alt_palette=[0,0x801F]+[0]*254
    header=f"""#ifndef T3_GENERATED_CHARACTER_H
#define T3_GENERATED_CHARACTER_H
#include <stdint.h>
#include "psx_pose.h"
#define T3_ARCHIVE_BYTES {len(archive)}u
#define T3_VERTEX_COUNT {vertices}u
#define T3_FACE_COUNT {faces}u
#define T3_JOINT_COUNT {joints}u
#define T3_TEXTURE_COUNT {report['textures']}u
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
extern const uint16_t t3_red_palette[256];
extern const uint8_t t3_kmd_double_sided[T3_FACE_COUNT];
extern const t3_psx_part_t t3_private_kmd_parts[T3_PSX_DRAW_PARTS];
extern const t3_psx_part_t* const t3_private_actor_parts[2];
#endif
"""
    payload=[
        '#include "t3_generated_character.h"',
        '/* Contains the user-supplied copyrighted KMD geometry. PRIVATE BUILD ONLY. */',
        't3_embedded_archive_t t3_archive = { .bytes = {'
    ]
    for i in range(0,len(archive),24):
        payload.append('    '+','.join(str(b) for b in archive[i:i+24])+',')
    payload.extend(['} };','const uint16_t t3_red_palette[256] = {'])
    for i in range(0,256,16):
        payload.append('    '+','.join(str(v) for v in alt_palette[i:i+16])+',')
    payload.append('};')
    payload.append('const uint8_t t3_kmd_double_sided[T3_FACE_COUNT] = {')
    flags=report['double_sided_faces']
    for i in range(0,len(flags),32):
        payload.append('    '+','.join(str(v) for v in flags[i:i+32])+',')
    payload.append('};')
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
    payload.append('const t3_psx_part_t* const t3_private_actor_parts[2] = {%s,t3_private_kmd_parts};' %
                   ('actor0_parts' if actor0_raw is not None else 't3_private_kmd_parts'))
    payload.extend(emit_c(json.loads((preview/"render_plan.json").read_text())))
    (output/"t3_generated_character.c").write_text('\n'.join(payload)+'\n',encoding='utf8')
    (output/"t3_generated_character.h").write_text(header,encoding='utf8')
    report.update(
        input_sha256=hashlib.sha256(raw).hexdigest(),
        actor0_skeleton_sha256=hashlib.sha256(actor0_raw or raw).hexdigest(),
        shared_render_mesh=True,
        scale_percent=model.scale_percent,
        local_kmd_bind_preview=True,
        distribution="PRIVATE, no commercial game data may enter Git or GitHub Actions")
    (output/"manifest.json").write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='utf8')
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kmd",type=Path,required=True)
    p.add_argument("--libsaturn-source",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--textures-arc",type=Path,help="private matching character ARC textures")
    p.add_argument("--actor0-kmd",type=Path,help="private P1 skeleton override; render mesh remains --kmd")
    a=p.parse_args()
    try:
        result=generate(a.kmd,a.libsaturn_source,a.output,a.textures_arc,a.actor0_kmd)
    except (OSError,ValueError,ImportError,AssertionError) as exc:
        p.exit(1,f"local KMD embed failed: {exc}\n")
    print(json.dumps({k:v for k,v in result.items() if k!='double_sided_faces'},sort_keys=True))

if __name__=="__main__":main()
