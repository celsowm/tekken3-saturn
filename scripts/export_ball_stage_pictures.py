#!/usr/bin/env python3
"""Export Tekken Ball's court pictures (stage 19, stg_v) from the USA disc with the pinned Tekken3Remake's converter:
stages/v/tiles.png (the 1024 x 256 backdrop strip), floor.png (10 x 10 tiles of 64 x 64), how_to.png (the RULES picture)
and stage.json. The output is derived from the game and stays out of git (assets/imported/).

    python3 scripts/export_ball_stage_pictures.py --image "roms/Tekken 3 USA/Tekken 3 (USA).cue" \\
        --out assets/imported/ball-probe/stage-usa
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/remake_import'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import common   # noqa: E402
import stage    # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--image', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    disc = common.Disc.open(args.image).canonical()
    out = common.Output(args.out)
    stage.convert_tiled(disc, out, stage.BALL_STAGE, 'v', 55)
    print(f'{out.written} files written, {out.unchanged} unchanged -> {args.out}/stages/v')


if __name__ == '__main__':
    main()
