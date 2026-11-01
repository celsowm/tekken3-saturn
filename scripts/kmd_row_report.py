#!/usr/bin/env python3
"""List a private KMD's rows: vertices, faces, double-sided faces, and faces per OT group."""
import argparse
from collections import Counter
from pathlib import Path

import kmd_to_ska


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kmd', type=Path)
    args = parser.parse_args()
    model = kmd_to_ska.parse_kmd(args.kmd.read_bytes())
    for row in model.rows:
        if row.vert is None:
            print(f'{row.row:2d} no mesh, parent {row.parent}')
            continue
        groups = Counter(group for group, _ in row.face_ot)
        print(f'{row.row:2d} parent {row.parent:2d} faces {len(row.faces):3d} double {sum(row.double_sided):3d} groups {dict(sorted(groups.items()))}'
              f' variants {sum(1 for v in row.variants if v)}')


if __name__ == '__main__':
    main()
