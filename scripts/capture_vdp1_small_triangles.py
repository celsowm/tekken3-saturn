#!/usr/bin/env python3
"""Extract authored VDP1 UV probe pixels; no game assets enter the fixture."""
import argparse
import json
from pathlib import Path
from PIL import Image

QUADS = [
    [(30,22),(36,18),(33,29),(33,29)],
    [(64,18),(66,24),(59,28),(59,28)],
    [(106,22),(99,27),(109,29),(109,29)],
    [(140,27),(144,18),(139,23),(139,23)],
    [(180,18),(184,24),(178,29),(178,29)],
    [(214,25),(210,18),(220,22),(220,22)],
    [(252,27),(258,23),(259,17),(259,17)],
    [(290,18),(281,23),(288,28),(288,28)],
]
NEW_QUADS = [
    [(28,23),(37,21),(35,32),(35,32)],
    [(61,17),(69,21),(57,32),(57,32)],
    [(106,16),(97,25),(112,28),(112,28)],
    [(139,27),(147,17),(136,22),(136,22)],
    [(179,16),(187,21),(177,31),(177,31)],
    [(215,26),(208,17),(222,23),(222,23)],
    [(250,28),(259,24),(262,16),(262,16)],
    [(292,17),(280,24),(287,32),(287,32)],
]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('screenshot', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--new', action='store_true')
    args = p.parse_args()
    image = Image.open(args.screenshot).convert('RGB')
    if image.size != (320, 224):
        raise ValueError('expected an unscaled 320x224 capture')
    cases = []
    for quad in NEW_QUADS if args.new else QUADS:
        left = min(x for x, _ in quad) - 1
        right = max(x for x, _ in quad) + 1
        top = min(y for _, y in quad) - 1
        bottom = max(y for _, y in quad) + 1
        planes = []
        for dy in (0, 100):
            points = {}
            for y in range(top, bottom + 1):
                for x in range(left, right + 1):
                    red, green, blue = image.getpixel((x, y + dy))
                    if green == 0 and blue == 248 and red in range(8, 65, 8):
                        points[(x, y)] = red // 8 - 1
            planes.append(points)
        if not planes[0] or planes[0].keys() != planes[1].keys():
            raise ValueError(f'UV planes differ for {quad}; capture may be occluded or blended')
        points = [[x, y, planes[0][x, y], planes[1][x, y]]
                  for x, y in sorted(planes[0], key=lambda pt: (pt[1], pt[0]))]
        cases.append({'vertices': quad, 'bounds': [left, top, right, bottom], 'points': points})
    args.output.write_text(json.dumps({'source': 'authored Ymir VDP1 UV probe', 'cases': cases}, indent=2) + '\n')
    print(f'{len(cases)} triangles, {sum(len(c["points"]) for c in cases)} pixels')

if __name__ == '__main__':
    main()
