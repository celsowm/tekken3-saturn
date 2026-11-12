#!/bin/sh
# Compare a resumed Tekken Force stretch against the original: run the three-fighter evaluator
# (built by build_oracle_pair_evaluator.py --fighters 4 21 22) from the RAM image that
# force_oracle_export.py --seed-at wrote (T3_FORCE_RAM_SEED: fighter records, move VM, spheres,
# random state, carried score and keys come from the image) and print the first differing row, ignoring
# one-row side_flag blips. Set T3_FORCE_REFILL=1 for the refill recordings (force_run, force_bosses, force_final).
#   scripts/force_ram_seed_eval.sh <evaluator> <export dir> <rand at the seed, 0x...> <level 1-4>
set -e
EVAL=$1; DIR=$2; RAND=$3; LEVEL=$4
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"; P=assets/imported/force-packs
T3_FORCE_START=1 T3_ORIGINAL_MATCH_START=$RAND T3_ORIGINAL_START_PADS="0 0" \
T3_FORCE_TABLES=$P/TABLES.BIN T3_FORCE_LEVEL=$P/LEV$LEVEL.BIN T3_FORCE_LEVEL_INDEX=$((LEVEL-1)) T3_FORCE_TRIG=$P/TRIG.BIN \
T3_FORCE_AI_TABLES=assets/imported/tekken3-usa/pose-tables/psx_ai_force.bin T3_FORCE_RAM_SEED=1 \
T3_FORCED_REACTION=assets/imported/tekken3-usa/pose-tables/psx_forced_reaction.bin \
  "$EVAL" "$DIR/seed.bin" "$DIR/cases.tsv" 0x8009E600 0x8009E610 0x8009E6F8 0x80097A44 --original-round-flow \
  > "$DIR/out.jsonl" 2> "$DIR/err.txt"
python3 - "$DIR" <<'PY'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1] + '/out.jsonl')]
real = lambda r: [x for x in r['differences'] if x['field'] != 'side_flag']
bad = [(n, r) for n, r in enumerate(rows) if real(r)]
print(len(rows), 'rows;', len(bad), 'differ (side_flag ignored)')
for n, r in bad[:1]:
    print(n, r['call'], [(x['fighter'], x['category'], x['field'], x['actual'], x['expected']) for x in real(r)[:20]])
PY
