#!/usr/bin/env python3
"""Tekken 3 PlayStation KMD -> LibSaturn SKA1 (local, offline, original data excluded).

KMD block/face traversal and PS1 two-anchor seam exchange, mapped into
LibSaturn's multi-anchor SourceModel. Exports SKA1 with a stationary 'bind'
clip. Optional character ARC input supplies original TIM/palette/UV patches;
otherwise a neutral placeholder texture is used. Authored motion is loaded
separately from DIVMOT at runtime. Morphs/material animation remain unsupported.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import sys

PART_ROWS=(0,1,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,21,22,23,24)
PART_MATRICES=(0,1,11,12,13,14,15,16,17,3,4,5,6,7,8,9,10,2,18,19,20,21)
SECOND_MESH={1,2,17}
GROUP_SIZES=(8,8,8,12)
GROUP_CORNERS=(3,4,3,4)
UV_SIZES=(4,5,4,5)
GROUP_NAMES=("FT3","FT4","GT3","GT4")
KMD_HEADER=0x10
ROW_BYTES=56
ROWS=27

class KmdError(ValueError): pass

@dataclass(frozen=True)
class VertBlock:
    imports_local: tuple[int,...]
    imports_global: tuple[int,...]
    coords: tuple[tuple[int,int,int],...]
    posts: tuple[tuple[int,...],...]

@dataclass(frozen=True)
class KmdRow:
    row: int
    words: tuple[int,...]
    vert: VertBlock | None
    faces: tuple[tuple[int,...], ...]
    materials: tuple[int,...]
    variants: tuple[int,...]
    double_sided: tuple[int,...] = ()
    face_uvs: tuple = ()
    @property
    def parent(self):
        return struct.unpack("<i",struct.pack("<I",self.words[8]))[0]
    @property
    def offset(self):
        return struct.unpack("<3i",struct.pack("<3I",*self.words[5:8]))

@dataclass(frozen=True)
class Kmd:
    scale_percent: int
    rows: tuple[KmdRow,...]

class Reader:
    def __init__(self,data,start,end):
        self.data=data;self.pos=start;self.end=end
    def take(self,count):
        if count<0 or self.pos+count>self.end:raise KmdError(f"block truncated at 0x{self.pos:x}")
        payload=self.data[self.pos:self.pos+count];self.pos+=count
        return payload
    def u32(self):return struct.unpack("<I",self.take(4))[0]
    def list(self,bits,per_word):
        n=self.u32()
        if n>8192:raise KmdError("unbounded packed array")
        packed=self.take(((n+per_word-1)//per_word)*4)
        words=struct.unpack("<"+str(len(packed)//4)+"I",packed)
        return tuple((words[i//per_word]>>(bits*(i%per_word)))&((1<<bits)-1) for i in range(n))

def parse_vertex(data,start,end,normals=False):
    rd=Reader(data,start,end)
    lead=rd.u32()
    local=rd.list(8,4);shared=rd.list(8,4)
    count=rd.u32()
    if count>4096:raise KmdError("unbounded KMD vertex count")
    coords=[]
    for _ in range(count):
        x,y,z,pad=struct.unpack("<4h",rd.take(8))
        if pad:raise KmdError("nonzero KMD SVECTOR padding")
        coords.append((x,y,z))
    groups=((8,4),(8,4)) if normals else ((9,3),)*5+((2,16),)
    post=tuple(rd.list(bits,width) for bits,width in groups)
    if rd.pos!=end:raise KmdError(f"vertex/normal block misaligned at 0x{rd.pos:x}")
    if not normals and lead!=2*(1+len(local)+len(shared)):
        raise KmdError("unexpected PS1 vertex import header")
    return VertBlock(local,shared,tuple(coords),post)

def parse_faces(data,start,end):
    rd=Reader(data,start,end);faces=[];counts=[];double_sided=[]
    for i,(length,corners) in enumerate(zip(GROUP_SIZES,GROUP_CORNERS)):
        count=rd.u32()
        if count>8192:raise KmdError("excessive KMD face count")
        counts.append(count)
        for _ in range(count):
            words=struct.unpack("<"+str(length//4)+"I",rd.take(length))
            w=words[0]
            vi=((w&0x1fc)>>2,(w>>9)&0x7f,(w>>16)&0x7f)
            if corners==4:vi+=((w>>25)&0x7f,)
            faces.append(vi)
            flags=words[1] if corners==4 else words[0]
            double_sided.append(int(bool(flags & 0x1000000)))
    if rd.pos!=end:raise KmdError("KMD faces have extra or truncated data")
    return tuple(faces),tuple(counts),tuple(double_sided)

def parse_texmap(data,start,end,counts,with_uv=False):
    rd=Reader(data,start,end)
    msize=struct.unpack("<H",rd.take(2))[0]
    if msize<2 or msize%2 or msize>4096:raise KmdError("invalid material header")
    mats=struct.unpack("<"+str(msize//2-1)+"H",rd.take(msize-2))
    uvsize=struct.unpack("<H",rd.take(2))[0]
    if uvsize<2 or uvsize%2 or uvsize>8192:raise KmdError("invalid UV table")
    uvs=rd.take(uvsize-2)
    if len(uvs)%2:raise KmdError("invalid UV array")
    face_materials=[];face_uvs=[]
    for gi,count in enumerate(counts):
        observed=rd.take(1)[0]
        if observed!=count:raise KmdError("KMD texture face count mismatch")
        for _ in range(count):
            fields=rd.take(UV_SIZES[gi])
            if fields[0]>=len(mats) or any(k>=len(uvs)//2 for k in fields[1:]):
                raise KmdError("invalid material/UV index")
            face_materials.append(int(mats[fields[0]]))
            face_uvs.append(tuple((uvs[2*k],uvs[2*k+1]) for k in fields[1:]))
    # Original Paul rows end immediately after the four groups, aligned to
    # four bytes. There is no mandatory two-byte trailer.
    padding=rd.take(end-rd.pos)
    if len(padding)>3 or any(padding):raise KmdError("unexpected KMD texture padding")
    return (tuple(face_materials),tuple(face_uvs)) if with_uv else tuple(face_materials)

def parse_kmd(data):
    if len(data)<KMD_HEADER+ROWS*ROW_BYTES or data[8:12]!=b"3DMK":
        raise KmdError("not a 27-row Tekken 3 KMD")
    count,scale=struct.unpack_from("<II",data)
    if count!=ROWS or not 1<=scale<=200:
        raise KmdError("invalid KMD row count or scale")
    words=[struct.unpack_from("<14I",data,KMD_HEADER+i*ROW_BYTES) for i in range(ROWS)]
    start_table=KMD_HEADER+ROWS*ROW_BYTES
    starts={v for row in words for v in row[:5] if v}
    variants={}
    for i,row in enumerate(words):
        if row[1]:
            if row[1]+41*4>len(data):raise KmdError("truncated hand variant offsets")
            variants[i]=struct.unpack_from("<41I",data,row[1])
            if variants[i][1]==0:
                starts.add(variants[i][0]) # special True Ogre external stride table
            else:
                starts.update(v for v in variants[i] if 0<v<len(data))
    if not starts or min(starts)!=start_table or any(v%4 or v>=len(data) for v in starts):
        raise KmdError("misaligned or inconsistent KMD block addresses")
    ordered=sorted(starts)+[len(data)]
    ends=dict(zip(ordered,ordered[1:]))
    parsed=[]
    for i,row in enumerate(words):
        if row[0]==0:
            if row[2] or row[3] or row[4]:raise KmdError("orphan KMD row data")
            parsed.append(KmdRow(i,row,None,(),(),tuple(variants.get(i,()))))
            continue
        if not all(row[v] in ends for v in (0,2,3,4)):
            raise KmdError("incomplete KMD mesh block pointers")
        vert=parse_vertex(data,row[0],ends[row[0]])
        parse_vertex(data,row[2],ends[row[2]],normals=True)
        faces,counts,double_sided=parse_faces(data,row[3],ends[row[3]])
        mats,uvs=parse_texmap(data,row[4],ends[row[4]],counts,with_uv=True)
        parsed.append(KmdRow(i,row,vert,faces,mats,tuple(variants.get(i,())),double_sided,uvs))
    return Kmd(scale,tuple(parsed))

def merge(a,b):
    out=dict(a)
    for term,w in b.items():out[term]=out.get(term,0.)+w
    return {term:w for term,w in out.items() if abs(w)>1e-10}

def halve(a):
    return {term:w/2 for term,w in a.items()}

def record_slots(kmd):
    """Simulate PS1 scratch/global exchange in draw-part order, returning per-corner anchors."""
    scratch={};shared={};out={};face_refs=[];variants_omitted=0
    for part,row_index in enumerate(PART_ROWS):
        if part>=18:continue # costume accessory enable table not yet decoded
        subrows=[row_index]+([row_index+1] if part in SECOND_MESH else [])
        for ri in subrows:
            row=kmd.rows[ri]
            if row.vert is None:continue
            vb=row.vert;cursor=0
            for off in vb.imports_local:
                key=off//2-1
                if key not in scratch:raise KmdError("unresolved scratch seam import")
                scratch[cursor]=dict(scratch[key]);cursor+=1
            for off in vb.imports_global:
                key=off//2-1
                if key not in shared:raise KmdError("unresolved global seam import")
                scratch[cursor]=dict(shared[key]);cursor+=1
            total=cursor+len(vb.coords)
            for vi,_ in enumerate(vb.coords):
                scratch[cursor+vi]={(part,ri,vi):1.0}
            groups=vb.posts
            p=cursor
            for e in groups[0]:
                key=(e&255)//2-1
                if key not in shared:raise KmdError("unresolved blend seam source")
                term=merge(halve(scratch[p]) if e&256 else scratch[p],shared[key])
                scratch[p]=shared[key]=term
                p+=1
            for grp,src in ((groups[1],shared),(groups[2],scratch)):
                for e in grp:
                    key=(e&255)//2-1
                    if key not in src:raise KmdError("unresolved add seam source")
                    scratch[p]=merge(halve(scratch[p]) if e&256 else scratch[p],src[key])
                    p+=1
            for grp,dst in ((groups[3],shared),(groups[4],scratch)):
                for e in grp:
                    if e&256:scratch[p]=halve(scratch[p])
                    dst[(e&255)//2-1]=dict(scratch[p])
                    p+=1
            for e in groups[5]:
                if e!=1:scratch[p]=halve(scratch[p])
                p+=1
            if p>total:raise KmdError("vertex post table exceeds own vertex count")
            out[part,ri]={slot:dict(scratch[slot]) for slot in range(total)}
            for face in row.faces:
                if any(slot not in out[part,ri] for slot in face):
                    raise KmdError(f"face uses undefined KMD slot in row {ri}")
                face_refs.append((part,ri,face))
            if row.variants:variants_omitted+=1
    return out,face_refs,variants_omitted

def to_source_model(kmd,lib_source,unit=1.0/4096):
    """Return LibSaturn's canonical host model with per-influence joint-local anchors."""
    source=(lib_source/"tools").resolve()
    if not (source/"model_pipeline"/"skeletal_export.py").is_file():
        raise KmdError("LibSaturn source checkout required (--libsaturn-source)")
    sys.path.insert(0,str(source))
    try:
        from model_pipeline.model import SourceModel,SourceNode,SourceSkin,AnimationClip
        model=SourceModel();model.skin_index=0
        global_rest=[]
        for part,ri in enumerate(PART_ROWS):
            row=kmd.rows[ri]
            par=row.parent
            if par>=part or par < -1:raise KmdError(f"invalid parent for draw part {part}: {par}")
            dx,dy,dz=row.offset
            # PS1 loads the file offset as (x,y,-z); D=(1,-1,-1)
            # then converts that runtime offset to Saturn's up-Y basis.
            translation=(dx*unit,-dy*unit,dz*unit)
            model.nodes.append(SourceNode(f"part_{part:02d}_matrix_{PART_MATRICES[part]:02d}",translation=translation))
            if par>=0:model.nodes[par].children.append(part)
            pivot=(tuple(global_rest[par][i]+translation[i] for i in range(3))
                   if par>=0 else translation)
            global_rest.append(pivot)
        model.skins=[SourceSkin(list(range(len(model.nodes))),[])]
        slots,face_refs,variant_count=record_slots(kmd)
        corners={}
        quads=[]
        for part,ri,face in face_refs:
            out=[]
            for slot in face:
                key=(part,ri,slot)
                if key not in corners:
                    anchors=slots[part,ri][slot]
                    if not anchors or len(anchors)>3:raise KmdError("unsupported PS1 skin anchor count")
                    weight=sum(anchors.values())
                    if not 0.999<=weight<=1.001:raise KmdError(f"non-unit KMD seam weights: {weight}")
                    weights=[]
                    x=[0.,0.,0.]
                    for (bone,row_id,idx),w in anchors.items():
                        vx,vy,vz=kmd.rows[row_id].vert.coords[idx]
                        local=(vx*unit,-vy*unit,-vz*unit)
                        weights.append((bone,w,local))
                        for axis in range(3):
                            x[axis]+=w*(global_rest[bone][axis]+local[axis])
                    corners[key]=len(model.vertices)
                    model.vertices.append(tuple(x))
                    model.skin_anchors.append(weights)
                    model.uvs.append((0.,0.))
                out.append(corners[key])
            if len(out)==3:
                quad=(out[0],out[1],out[2],out[2])
                model.triangles.append((out[0],out[1],out[2]))
            else:
                # PS1 quads are a strip (012,132); VDP1 expects perimeter
                # order. Sending 0123 crosses the final two edges.
                quad=(out[0],out[1],out[3],out[2])
                model.triangles.extend(((out[0],out[1],out[2]),(out[1],out[3],out[2])))
            quads.append(quad)
            model.tri_materials.extend([0]*(1 if len(out)==3 else 2))
        if not model.vertices or not quads:raise KmdError("KMD produced no visible model geometry")
        model.clips=[AnimationClip("bind",duration=0.1)]
        return model,quads,variant_count
    finally:
        sys.path.remove(str(source))

def compile_ska(kmd,source,output,texture_arc=None):
    model,quads,variants_omitted=to_source_model(kmd,source)
    double_sided=[]
    for part,row_index in enumerate(PART_ROWS[:18]):
        for ri in [row_index]+([row_index+1] if part in SECOND_MESH else []):
            row=kmd.rows[ri]
            if row.vert is not None:
                double_sided.extend(row.double_sided or (0,)*len(row.faces))
    if len(double_sided)!=len(quads):raise KmdError('face flag/geometry order disagrees')
    sys.path.insert(0,str((source/"tools").resolve()))
    try:
        from model_pipeline import skeletal_export as sx
        from model_pipeline.skeletal_binary import encode
        from model_pipeline.static_import import ImportResult
        export=sx.export_model(model,rate=20)
        verified=sx.verify(model,export)
        remap=export.skin.source_to_vertex
        bounds=[[round(65536*extreme(v[i] for v in model.vertices)) for i in range(3)]
                for extreme in (min,max)]
        # Intentionally neutral ORIGINAL/author-created display-only texture.
        texture={"width":8,"height":8,"pixels":bytes([1]*64),"flags":1,"pixel_count":64}
        render=ImportResult(
            [tuple(round(v*65536) for v in model.vertices[sv]) for sv in export.skin.source_vertex],
            [tuple(remap[i] for i in q) for q in quads],
            [0]*len(quads),[texture],[0,0xBDEF]+[0]*254,1,
            {"source_bounds_fx":bounds})
        texture_report={}
        if texture_arc is not None:
            from psx_character_textures import load_character_vram,bake_face
            from model_pipeline.texture_bake import build_shared_palette,map_faces_to_indices
            vram,tim_count=load_character_vram(texture_arc.read_bytes())
            rgba=[]
            for part,row_index in enumerate(PART_ROWS[:18]):
                for ri in [row_index]+([row_index+1] if part in SECOND_MESH else []):
                    row=kmd.rows[ri]
                    if row.vert is not None:
                        for material,uv in zip(row.materials,row.face_uvs):
                            rgba.append(bake_face(vram,material,uv))
            if len(rgba)!=len(quads):raise KmdError('texture/geometry face order disagrees')
            palette,transparent,_=build_shared_palette(rgba)
            indexed=map_faces_to_indices(rgba,[(8,8)]*len(rgba),palette)
            unique=[];lookup={};face_textures=[]
            for pixels in indexed:
                data=bytes(pixels)
                if data not in lookup:
                    lookup[data]=len(unique)
                    unique.append(dict(width=8,height=8,pixels=data,
                                       flags=0 if transparent else 1,pixel_count=64))
                face_textures.append(lookup[data])
            colors=[0 if transparent and i==0 else
                    0x8000|((r*31+127)//255)|((g*31+127)//255)<<5|((b*31+127)//255)<<10
                    for i,(r,g,b) in enumerate(palette)]
            colors=(colors+[0]*256)[:256]
            render.textures=unique
            render.face_texture_indices=face_textures
            render.palette_rgb555=colors
            texture_report=dict(tim_uploads=tim_count,textures=len(unique),
                                indexed_texture_bytes=64*len(unique),
                                texture_patch_size=[8,8],source_arc_sha256=hashlib.sha256(texture_arc.read_bytes()).hexdigest())
        blob=encode(export,render,[node.name for node in model.nodes])
        from compiled_render_plan import compile_plan
        render_plan=compile_plan(export.skin,render.indices_abcd,double_sided)
    finally:
        sys.path.remove(str((source/"tools").resolve()))
    output.mkdir(parents=True,exist_ok=True)
    (output/"fighter_bind.ska").write_bytes(blob)
    (output/"render_plan.json").write_text(
        json.dumps(render_plan,indent=2,sort_keys=True)+'\n',encoding='utf8')
    with (output/"fighter_bind.obj").open("w",encoding="utf8") as f:
        f.write("# Untextured KMD rest-pose preview; coordinates require orientation validation\n")
        for v in model.vertices:f.write("v "+ " ".join(f"{x:.7f}" for x in v)+"\n")
        for q in quads:
            ids=q[:3] if q[2]==q[3] else q
            f.write("f "+" ".join(str(i+1) for i in ids)+"\n")
    return {"schema":1,"format":"SKA1","source":"Tekken 3 KMD / 3DMK",
            "joints":len(model.nodes),"faces":len(quads),
            "logical_vertices":len(model.vertices),
            "compiled_vertices":len(export.skin.source_vertex),
            "blend_vertices":len(export.skin.blend),
            "render_plan":{k:render_plan[k] for k in
                ('schema','joint_mask','maximum_l1','exact_skin_supported')},
            "double_sided_faces":double_sided,
            "variant_rows_pending":variants_omitted,
            "original_textures":texture_arc is not None,"original_motion":False,
            "textures":len(render.textures),"texture_report":texture_report,
            "clip":["bind"],"archive_bytes":len(blob),
            "archive_sha256":hashlib.sha256(blob).hexdigest(),
            "export_verify":verified}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kmd",type=Path,required=True,
                   help="Local raw BNS record, e.g. assets/imported/tekken3-usa/raw/071.bin")
    p.add_argument("--libsaturn-source",type=Path,required=True)
    p.add_argument("--output",type=Path,default=Path("assets/imported/tekken3-usa/paul"))
    a=p.parse_args()
    try:
        data=a.kmd.read_bytes()
        kmd=parse_kmd(data)
        report=compile_ska(kmd,a.libsaturn_source,a.output)
        report.update(input_sha256=hashlib.sha256(data).hexdigest(),
                      scale_percent=kmd.scale_percent)
        (a.output/"manifest.json").write_text(
            json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf8")
    except (OSError,ValueError,ImportError,struct.error) as e:
        p.exit(1,f"KMD conversion failed: {e}\n")
    print(json.dumps(report,sort_keys=True))

if __name__=="__main__":main()
