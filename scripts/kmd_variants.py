#!/usr/bin/env python3
"""Report each roster fighter's KMD vertex variants (hand poses, jaws, wings).

For every row with a variant table (3dmk-models.md, vertVariants) it prints the distinct
blocks and whether each has the base block's import and post tables and own vertex count:
a block that does can be shown by rewriting vertex positions only. Private data; prints
counts and shapes, never coordinates.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fighter_roster import ROSTER, records  # noqa: E402
from kmd_to_ska import parse_vertex, parse_kmd  # noqa: E402


def block_ends(data, kmd):
    """Every block start in the file, for the end of a variant block."""
    starts = set()
    for row in kmd.rows:
        starts.update(v for v in row.words[:5] if v)
        if row.variants and row.variants[1]:
            starts.update(v for v in row.variants if 0 < v < len(data))
        if row.words[1]:
            starts.add(row.words[1])
    ordered = sorted(starts) + [len(data)]
    return dict(zip(ordered, ordered[1:]))


def variants(data):
    """{row: [(variant, same_shape, moved_vertices) per non-zero entry]}; None for strides."""
    kmd = parse_kmd(data)
    ends = block_ends(data, kmd)
    out = {}
    for row in kmd.rows:
        if not row.variants or row.vert is None:
            continue
        if row.variants[1] == 0:
            out[row.row] = None  # True Ogre's wing strides into a runtime buffer
            continue
        base = row.vert
        found = []
        for index, at in enumerate(row.variants):
            if not at:
                continue
            block = parse_vertex(data, at, ends[at])
            same = (block.imports_local == base.imports_local and block.imports_global == base.imports_global
                    and block.posts == base.posts and len(block.coords) == len(base.coords))
            moved = sum(a != b for a, b in zip(block.coords, base.coords)) if same else -1
            found.append((index, same, moved))
        out[row.row] = found
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--raw', type=Path, required=True, help='the private import raw/ directory')
    args = ap.parse_args()
    for fighter in ROSTER:
        data = (args.raw / f"{records(fighter)['kmd']:03}.bin").read_bytes()
        for row, found in variants(data).items():
            if found is None:
                print(f'{fighter.id:2} {fighter.name:12} row {row:2}: runtime strides')
                continue
            odd = [i for i, same, _ in found if not same]
            moved = sorted({m for _, same, m in found if same})
            print(f'{fighter.id:2} {fighter.name:12} row {row:2}: {len(found)} entries, '
                  f'moved {moved[0] if moved else 0}-{moved[-1] if moved else 0}'
                  + (f', other shape {odd}' if odd else ''))


if __name__ == '__main__':
    main()
