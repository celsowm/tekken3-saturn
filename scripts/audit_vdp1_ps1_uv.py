#!/usr/bin/env python3
"""Compare captured VDP1 synthetic UVs with PS1 affine-triangle estimates.

The captured fixtures contain authored 8x8 textures, not game data. PS1
values here are a geometric model, not captures of the PlayStation GPU.
"""
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def affine(point, vertices, triangle):
    # VDP1 A,B,C,D correspond to PS1 quad v0,v1,v3,v2.
    uv=((0,0),(7,0),(7,7),(0,7))
    (x,y)=point
    (a,b,c)=[vertices[i] for i in triangle]
    area=(b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1])
    if not area:return None
    weights=(((b[0]-x)*(c[1]-y)-(c[0]-x)*(b[1]-y))/area,
             ((c[0]-x)*(a[1]-y)-(a[0]-x)*(c[1]-y))/area)
    weights=(*weights,1-weights[0]-weights[1])
    # Exclude edges: the two consoles' fill rules can differ there.
    if min(weights)<=1e-8:return None
    return tuple(max(0,min(7,math.floor(sum(weights[k]*uv[triangle[k]][axis]
                                         for k in range(3))+1e-8))) for axis in (0,1))


def main():
    fixture=json.loads((ROOT/'tests/fixtures/vdp1_distorted_uv.json').read_text())
    report=[]
    for case in fixture['cases']:
        vertices=case['vertices']
        count=mismatch=total=maximum=0
        for x,y,u,v in case['points']:
            expected=affine((x,y),vertices,(0,1,3)) or affine((x,y),vertices,(1,2,3))
            if expected is None:continue
            error=abs(expected[0]-u)+abs(expected[1]-v)
            count+=1;mismatch+=error!=0;total+=error;maximum=max(maximum,error)
        if count:
            report.append({'vertices':vertices,'interior_samples':count,'uv_mismatches':mismatch,
                           'mean_manhattan_texel_error':round(total/count,4),'max_texel_error':maximum})
    print(json.dumps({'source':fixture['source'],'note':'PS1 affine estimate; excludes triangle edges',
                      'cases':report},indent=2))


if __name__=='__main__':
    main()
