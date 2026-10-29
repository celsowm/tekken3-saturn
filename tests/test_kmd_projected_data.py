#!/usr/bin/env python3
"""Audit private generated projected blocks and their SKA face-index links."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import struct
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
import kmd
PART_ROWS = (0,1,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19)
SECOND = {1,2,17}

def array(source, name):
    m = re.search(r'static const (?:uint8_t|uint16_t|int16_t) ' + re.escape(name) + r'\[\](?:\[3\])? = \{(.*?)\};', source)
    return [int(x) for x in re.findall(r'-?\d+', m.group(1))] if m else []

def audit(source, model_path, archive_path, suffix):
    model = kmd.parse(model_path.read_bytes())
    blob = archive_path.read_bytes()
    header = struct.unpack_from('>24I', blob)
    if header[0] != 0x534b4131:
        raise AssertionError('invalid SKA archive')
    face_count, face_offset = header[7], header[17]
    archive_faces = [struct.unpack_from('>4H', blob, face_offset + 8*i) for i in range(face_count)]
    all_faces, logical_faces, mappings, logical_values, blocks = [], [], [], [], 0
    for part, row_index in enumerate(PART_ROWS):
        for ri in [row_index] + ([row_index+1] if part in SECOND else []):
            row = model.rows[ri]
            vb = row.verts
            if vb is None:
                continue
            blocks += 1
            prefix = f't3_kmd_projected{suffix}_{part}_{ri}'
            checks = [('local', vb.import_scratch), ('global', vb.import_global),
                      ('xyz', [v for xyz in vb.coords for v in xyz])]
            checks += [(f'post{i}', values) for i, values in enumerate(vb.post)]
            for label, expected in checks:
                if array(source, prefix + '_' + label) != list(expected):
                    raise AssertionError(f'{prefix} {label} differs from original model')
            raw_map = array(source, prefix + '_map')
            if len(raw_map) % 2:
                raise AssertionError('malformed slot map')
            mapping = dict(zip(raw_map[::2], raw_map[1::2]))
            if len(mapping) * 2 != len(raw_map):
                raise AssertionError('duplicate slot')
            mappings.extend(mapping.values())
            logical_raw = array(source, prefix + '_logical')
            if len(logical_raw) != len(raw_map) or logical_raw[::2] != raw_map[::2]:
                raise AssertionError('logical slot map differs from compiled slot map')
            logical = dict(zip(logical_raw[::2], logical_raw[1::2]))
            logical_values.extend(logical.values())
            for face in row.faces or []:
                vertices = [mapping[v] for v in face.verts]
                all_faces.append(tuple(vertices + [vertices[-1]]) if len(vertices) == 3 else
                                 (vertices[0], vertices[1], vertices[3], vertices[2]))
                vertices = [logical[v] for v in face.verts]
                logical_faces.append(tuple(vertices + [vertices[-1]]) if len(vertices) == 3 else
                                     (vertices[0], vertices[1], vertices[3], vertices[2]))
    if all_faces != archive_faces:
        raise AssertionError('projected mappings differ from SKA face indices')
    if sorted(logical_values) != list(range(len(logical_values))):
        raise AssertionError('logical projected slots are not distinct and contiguous')
    match = re.search(r'const uint16_t t3_kmd_projected' + suffix + r'_faces\[\]\[4\] = \{(.*?)\};', source)
    if not match:
        raise AssertionError('missing logical projected faces')
    raw_faces = [int(x) for x in re.findall(r'\d+', match.group(1))]
    if raw_faces != [v for face in logical_faces for v in face]:
        raise AssertionError('logical projected faces differ from original model')
    counts = Counter(mappings)
    return dict(blocks=blocks, mapped_slots=len(mappings), shared_indices=sum(n > 1 for n in counts.values()), faces=len(all_faces))

def main():
    p = argparse.ArgumentParser(); p.add_argument('--generated', type=Path, required=True); p.add_argument('--preview', type=Path, required=True); a = p.parse_args()
    models = [ROOT/'assets/imported/tekken3-usa/raw/071.bin', ROOT/'assets/imported/law-import/raw/079.bin']
    archives = [a.preview/'kmd_preview/fighter_bind.ska', a.preview/'kmd_preview1/fighter_bind.ska']
    if not all(x.is_file() for x in [a.generated, *models, *archives]): return 77
    source = a.generated.read_text()
    print(json.dumps([audit(source, m, archive, suffix) for m, archive, suffix in zip(models, archives, ('','1'))]))
    return 0
if __name__ == '__main__': raise SystemExit(main())
