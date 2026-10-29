#!/usr/bin/env python3
"""Compare preserved fighter face metadata with the independent remake parser."""
from collections import Counter
import argparse
import re
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import kmd_to_ska as ours
sys.path.insert(0,str(ROOT/'third_party/Tekken3Remake/tools/research'))
import kmd as reference

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--generated',type=Path);args=parser.parse_args()
    paths=[ROOT/'assets/imported/tekken3-usa/raw/071.bin',ROOT/'assets/imported/law-import/raw/079.bin']
    if not all(p.is_file() for p in paths):return 77
    count=0;groups=Counter();biases=Counter();expected_generated=[]
    for path in paths:
        raw=path.read_bytes();a=ours.parse_kmd(raw);b=reference.parse(raw)
        expected=[]
        for part,row_index in enumerate(ours.PART_ROWS[:18]):
            for ri in [row_index]+([row_index+1] if part in ours.SECOND_MESH else []):
                row=b.rows[ri]
                if row.verts is not None:
                    for face in row.faces or []:
                        kind=ours.GROUP_NAMES.index(face.kind.upper())
                        high=((face.z_bias & 127)<<1)|int(face.double_sided)
                        expected.append((kind,high))
        expected_generated.append(expected)
        for row,other in zip(a.rows,b.rows):
            faces=other.faces or []
            if len(row.faces)!=len(faces) or len(row.face_ot)!=len(faces):raise AssertionError('face count mismatch')
            for vertices,(group,high),face in zip(row.faces,row.face_ot,faces):
                bias=high>>1
                if bias&64:bias-=128
                if (vertices!=face.verts or ours.GROUP_NAMES[group].lower()!=face.kind or
                    bool(high&1)!=face.double_sided or bias!=face.z_bias):
                    raise AssertionError(f'face metadata differs: {path} row {row.row}')
                count+=1;groups[face.kind]+=1;biases[bias]+=1
    if args.generated:
        source=args.generated.read_text()
        for symbol,expected in zip(('t3_kmd_face_ot','t3_kmd_face_ot1'),expected_generated):
            m=re.search(r'const uint8_t '+symbol+r'\[T3_FACE_COUNT\]\[2\] = \{(.*?)\n\};',source,re.S)
            if not m:raise AssertionError('missing generated metadata '+symbol)
            actual=[tuple(map(int,pair)) for pair in re.findall(r'\{(\d+),(\d+)\}',m.group(1))]
            if actual!=expected:raise AssertionError('generated metadata order differs: '+symbol)
    print(json.dumps(dict(faces=count,groups=dict(groups),biases=dict(sorted(biases.items())),
                          scope='Private USA Paul and Law KMD metadata compared with pinned remake parser')))
    return 0
if __name__=='__main__':raise SystemExit(main())
