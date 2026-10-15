# Tekken 3 PS1 move logic — DIVMOT milestone 1

The full original fighting logic is **not yet ported**. This milestone builds
its essential portable mechanisms in independent, testable C code. No Godot
engine, proprietary PS1 MIPS code or copyrighted content is copied into
this MIT codebase.

## What is implemented

- `src/game/divmot.[ch]` — validates the raw PlayStation DIVMOT bank
  header (15 ordered section boundaries, 56-byte move rows, 12-byte branch
  rows, 4,023 move slots). Supports bank 0–20 and shared bank 99. Decodes
  move state/attack words, action window, damage, stance, flags and reactions.
  Local/common slot lookup also reports its owning bank, so common moves use the correct branch table. Slot lookup observes `0x3FFFFFFF` (empty) and
  `0x40000000` (common namespace).
- `src/game/input_history.[ch]` — each player has 60 frames of input history,
  Tekken-style numpad directions relative to the opponent, four attack
  buttons, double-tap 6-5-6 / 4-5-4, simple command words, and a strict
  motion sequence matching primitive.
- `src/game/move_vm.[ch]` — deterministic move state machine: starts from
  a character-defined stance slot, advances move frames, executes the
  first valid *currently supported* branch, and resumes through the
  terminating row's default target.
- `src/game/fight.[ch]` — prototype is now using the command history for
  dash, high/mid/low blocking, crouch and jump. These four fallback
  attacks **still are not a real Tekken moveset**.

## Export a logic-only bank from a local disc

Original game data must stay in gitignored `assets/imported` (or a private
directory). Typical USA format: BNS record 98 = the first costume-slot's
character DIVMOT bank; record 73 = shared `divmot99`.

```sh
python3 scripts/psx_disc_import.py \
  --source '/path/to/Tekken 3 (USA).zip' \
  --extract-ids 73,98
python3 scripts/divmot_to_logic.py \
  --bank assets/imported/tekken3-usa/raw/098.bin \
  --output assets/imported/tekken3-usa/moves
python3 scripts/divmot_to_logic.py \
  --bank assets/imported/tekken3-usa/raw/073.bin \
  --output assets/imported/tekken3-usa/moves
```

The output `bank_*.t3logic` keeps only sections 0, 1 and 2 (branches,
moves, slot indices) and a rewritten 64-byte header. Its source motion
offsets are *metadata only*; all animation, sounds, hitbox records, events,
camera and textures are omitted. The resulting compact file can be opened
by `t3_divmot_open`, **but is not a standalone game and cannot drive the
real combat without the missing sections**.

## Optional Saturn move-interpreter diagnostic (private)

The experimental game can embed a compact bank **locally** for interpreting
input/branch transitions and displaying live slots on the Z profiler:

```sh
export LIBSATURN_PREFIX=/path/to/libsaturn-install
cmake --preset saturn \
  -DLIBSATURN_SOURCE_DIR=/path/to/libsaturn \
  -DT3_LOCAL_DIVMOT="$PWD/assets/imported/tekken3-usa/moves/bank_0.t3logic" \
  -DT3_LOCAL_COMMON_DIVMOT="$PWD/assets/imported/tekken3-usa/moves/bank_99.t3logic"
cmake --build --preset saturn
```

These paths are examples: replace `bank_0` with the bank type shown by
your import manifest. Both fighters currently run **the same** reference bank
in a diagnostic-only VM. The core gameplay still uses its prototype attacks.
The profiler shows PS1 move slots for investigation; `g_t3_diagnostics`
(version 2) also records slots, frames, attack words and transition counts.
The VM starts at original stance slot 3 when defined, otherwise 0.

Because private output embeds original move data, its generated C/ELF/ISO
must **never be published**. Public CI uses only synthetic test banks.
Clear `T3_LOCAL_DIVMOT` and `T3_LOCAL_COMMON_DIVMOT` and rebuild before
creating any public artifact.

## Next major work, in dependency order

1. Validate 22 banks and all available row/branch references from the US
   disc (on a private machine with a game copy).
2. Implement all original input sequence tables, branch restrictions and
   condition types (not just condition 0). Add strict unknown-opcode diagnostics.
3. Link character and shared banks into two combat actors; data-driven
   authoritative motion selection instead of four fabricated attack entries.
4. Decode original motion banks into LibSaturn skeleton keyframes, hit
   segment records and per-frame state changes.
5. Reproduce original grounded/air reactions, counter hits, throws,
   juggles, ground recovery, environment limits, frame events, sound,
   dynamic difficulty CPU, team/force/ball modes and original UI/camera.
6. Compare frame-by-frame state traces against the upstream game's
   PS1 oracle / Godot remake and fix differences systematically.

**Technical boundary:** the supported DIVMOT branch executor currently
accepts only zero-condition, zero-restriction rows and implemented command
words; unknown command opcodes and conditions safely do not match.
The original authored data can be copyrighted even when reduced to move
tables: do not commit the generated output or upload public builds embedding
it. The submodule's PolyForm Noncommercial license applies independently.

Format references:
[DIVMOT banks](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/formats/divmot-banks.md),
[move lifecycle](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/code/moves.md),
[combat](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/code/combat.md).
