#!/usr/bin/env python3
"""Count private KMD faces eligible for rigid-part preparation; export no mesh."""
import argparse,collections,hashlib,json
from pathlib import Path
from kmd_to_ska import PART_ROWS,SECOND_MESH,parse_kmd,to_source_model


def classify(rigid_joints,vertex_count,faces,double_sided):
    if len(faces)!=len(double_sided) or len(rigid_joints)>vertex_count:
        raise ValueError('inconsistent skin topology')
    counts=collections.Counter()
    rigid_part_vertices=set()
    cull_plan_vertices=set()
    other_face_vertices=set()
    for face,two_sided in zip(faces,double_sided):
        if len(face)!=4 or any(v<0 or v>=vertex_count for v in face):
            raise ValueError('invalid quad vertex')
        rigid=all(v<len(rigid_joints) for v in face)
        one_part=rigid and len({rigid_joints[v] for v in face})==1
        kind='single_rigid_joint' if one_part else 'multiple_rigid_joints' if rigid else 'has_blend'
        counts[kind]+=1
        if not two_sided:counts[kind+'_cullable']+=1
        if one_part:rigid_part_vertices.update(face)
        if one_part and not two_sided:cull_plan_vertices.update(face)
        else:other_face_vertices.update(face)
    return dict(vertices=vertex_count,rigid_vertices=len(rigid_joints),
                blend_vertices=vertex_count-len(rigid_joints),faces=len(faces),
                face_classes=dict(counts),single_rigid_joint_referenced_vertices=len(rigid_part_vertices),
                vertices_exclusive_to_cullable_single_joint_faces=len(cull_plan_vertices-other_face_vertices),
                scope='Static eligibility only. No runtime visibility, normal-precision proof, '
                      'saved vertex count, timing or pixel-equivalence claim.')


def audit(kmd_path,libsaturn):
    raw=kmd_path.read_bytes();kmd=parse_kmd(raw)
    model,quads,_=to_source_model(kmd,libsaturn)
    from model_pipeline.skeletal_export import export_model
    skin=export_model(model,rate=20).skin
    flags=[]
    for part,row_index in enumerate(PART_ROWS[:18]):
        for ri in [row_index]+([row_index+1] if part in SECOND_MESH else []):
            row=kmd.rows[ri]
            if row.vert is not None:flags.extend(row.double_sided or (0,)*len(row.faces))
    faces=[tuple(skin.source_to_vertex[v] for v in face) for face in quads]
    positions=[pos for pos,joint in skin.rigid]
    positions.extend(pos for joints,count,weights,anchors in skin.blend for pos in anchors[:count])
    max_l1=max(sum(abs(value) for value in pos) for pos in positions)
    round_shift=12+skin.pos_shift-16
    round_bias=1<<(round_shift-1) if round_shift>0 else 0
    blends=collections.Counter('two_equal_half' if count==2 and tuple(weights[:2])==(1<<14,1<<14)
                               else str(count)+'_weighted'
                               for joints,count,weights,anchors in skin.blend)
    return dict(kmd_sha256=hashlib.sha256(raw).hexdigest(),
                position_shift=skin.pos_shift,maximum_influence_position_l1=max_l1,
                q12_full_signed_rotation_dot_bound=max_l1*(1<<15),
                q12_rotation_round_shift=round_shift,q12_rotation_round_bias=round_bias,
                q12_rounded_dot_fits_i32=max_l1*(1<<15)+round_bias<=(1<<31)-1,
                blend_classes=dict(blends),
                **classify([joint for pos,joint in skin.rigid],
                           len(skin.rigid)+len(skin.blend),faces,flags))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kmd',type=Path,required=True)
    parser.add_argument('--libsaturn',type=Path,default=Path(__file__).resolve().parents[2]/'lib-saturn')
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    args.out.write_text(json.dumps(audit(args.kmd,args.libsaturn),indent=2)+'\n')
