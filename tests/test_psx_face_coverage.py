#!/usr/bin/env python3
"""Compare local 8x8 fighter texture opacity to the remake's original VRAM."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'third_party/Tekken3Remake/tools/remake_import'),str(ROOT/'third_party/Tekken3Remake/tools/research')]
try:
    import character
    import kmd
except ImportError:
    raise SystemExit(77)

def expected_patch(vram,tex,face,pages):
    material=tex.materials[face.material]
    page_x,page_y,depth,clut_x,clut_y=character.material_page(material)
    key=(page_x,page_y,depth)
    if key not in pages:pages[key]=vram.page_indices(*key)
    page=pages[key];uv=[tex.uvs[i] for i in face.uvs]
    corners=(uv[0],uv[1],uv[2],uv[2]) if len(uv)==3 else (uv[0],uv[1],uv[3],uv[2])
    rows=[]
    for y in range(8):
        bits=0;fy=y/7
        for x in range(8):
            fx=x/7;weights=((1-fx)*(1-fy),fx*(1-fy),fx*fy,(1-fx)*fy)
            u,v=(round(sum(p[a]*w for p,w in zip(corners,weights)))&255 for a in range(2))
            index=int(page[v,u]);color=int(vram.words[clut_y,clut_x+index]);bits=(bits<<1)|int(color!=0)
        rows.append(bits)
    return rows

def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--preview',type=Path,required=True);a=p.parse_args()
    models=[ROOT/'assets/imported/tekken3-usa/raw/071.bin',ROOT/'assets/imported/law-import/raw/079.bin']
    arcs=[ROOT/'assets/imported/tekken3-usa/raw/073.bin',ROOT/'assets/imported/law-import/raw/081.bin']
    archives=[a.preview/'kmd_preview/fighter_bind.ska',a.preview/'kmd_preview1/fighter_bind.ska']
    if not all(p.is_file() for p in models+arcs+archives):return 77
    payload=[];expected=[];stats=Counter()
    for model_path,arc_path,archive_path in zip(models,arcs,archives):
        model=kmd.parse(model_path.read_bytes());vram=character.load_textures(character.arc_members(arc_path.read_bytes())[0]);pages={}
        blob=archive_path.read_bytes();header=struct.unpack_from('>24I',blob)
        if header[0]!=0x534b4131:raise AssertionError('bad SKA')
        count,texture_count,face_at,texture_at=header[7],header[8],header[18],header[19]
        indices=struct.unpack_from(f'>{count}H',blob,face_at)
        textures=[]
        for i in range(texture_count):
            offset,width,height,flags,_,size=struct.unpack_from('>IHHHHI',blob,texture_at+16*i)
            if (width,height,size)!=(8,8,64) or flags&0x8000:raise AssertionError('unsupported texture format')
            textures.append(blob[offset:offset+64])
        face_index=0
        for part,row in enumerate(kmd.PART_ROW[:18]):
            for ri in [row]+([row+1] if part in (1,2,17) else []):
                tex=model.rows[ri].tex
                if tex is None:continue
                for face in tex.faces:
                    pixel=textures[indices[face_index]]
                    if len(pixel)!=64:raise AssertionError('truncated texture')
                    payload.append((0,*pixel));expected.append(expected_patch(vram,tex,face,pages));face_index+=1
        if face_index!=count:raise AssertionError('face count mismatch')
        stats[model_path.name]=face_index
    run=subprocess.run([str(a.runner.resolve())],input=''.join(' '.join(map(str,r))+'\n' for r in payload),text=True,capture_output=True,check=True)
    got=[json.loads(line) for line in run.stdout.splitlines()]
    if got!=expected:
        for i,(g,e) in enumerate(zip(got,expected)):
            if g!=e:raise AssertionError(f'patch {i}: {g} != {e}')
        raise AssertionError('patch count mismatch')
    print(json.dumps(dict(faces=sum(stats.values()),patch_pixels=sum(stats.values())*64,models=dict(stats),scope='baked texture opacity, not screen coverage')))
    return 0
if __name__=='__main__':raise SystemExit(main())
