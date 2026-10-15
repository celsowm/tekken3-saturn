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

By default the output `bank_*.t3logic` keeps only sections 0, 1 and
2 (branches, moves and slot indices) with a rewritten 64-byte header.
Passing `--with-hitboxes` additionally retains section 9's original
attack geometry. The original animation offsets remain *metadata*;
animation, sound, frame events, camera and textures are omitted. The resulting compact file can be opened
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
(version 3) also records slots, frames, attack words and transition counts.
The VM starts at original stance slot 3 when defined, otherwise 0.

Because private output embeds original move data, its generated C/ELF/ISO
must **never be published**. Public CI uses only synthetic test banks.
Clear `T3_LOCAL_DIVMOT` and `T3_LOCAL_COMMON_DIVMOT` and rebuild before
creating any public artifact.

## Extract original PS1 input commands (private, opt-in)

The game's EXE has **104 indirect command sequences**: 63 commands
`0xC00E..0xC04C` and 41 commands `0xC7FF..0xC827`.
The USA and Japan executables have different layouts: rather than assume
Japanese addresses, `extract_psx_sequences.py` searches for exactly 104
structurally valid pointers and validates each signed time window and
terminated 16-bit direction/button sequence. It validates the supported
USA executable SHA-256. It does not ship executable data.

```sh
python3 scripts/psx_disc_import.py \
    --source '/path/to/Tekken 3 (USA).zip' \
    --dump-exe --extract-ids 73,98
python3 scripts/extract_psx_sequences.py \
    --exe assets/imported/tekken3-usa/SLUS_004.02 \
    --output assets/imported/tekken3-usa/moves/input_sequences.json
python3 scripts/divmot_to_logic.py \
    --bank assets/imported/tekken3-usa/raw/098.bin \
    --output assets/imported/tekken3-usa/moves
python3 scripts/divmot_to_logic.py \
    --bank assets/imported/tekken3-usa/raw/073.bin \
    --output assets/imported/tekken3-usa/moves
```

To run the sequences in the Saturn debug interpreter, configure a **private**
build with `-DT3_LOCAL_DIVMOT=...`,
`-DT3_LOCAL_COMMON_DIVMOT=...` and
`-DT3_LOCAL_PSX_SEQUENCES="$PWD/assets/imported/tekken3-usa/moves/input_sequences.json"`.
The existing `t3_move_vm_tick_ex` passes the table to the input matcher;
the debug VM does not yet replace the ordinary combat gameplay. **Do not
publish the executable, extracted table, generated C, ELF or ISO.**

The source parser and tables have synthetic regression tests, but this
exact ZIP/revision has **not yet been run through the importer here**.
An unsupported EXE SHA, ambiguous table search, or a sequence exceeding the
implementation's 32-step / 60-frame limit results in an explicit error.

## PS1 branch predicates and input sequence registry

The separate `branch_rules.[ch]` evaluator now accepts original DIVMOT
character/bank restrictions and documented condition types. Its input is a
**fact record with explicit availability bits**, since the prototype's Q8
positions and hitboxes are not the original game's calibrated data. An
unknown fact returns false, not a guessed positive match.

Implemented with explicit facts: contact / hit / guard / whiff, adjusted
throw situations, distance ranges, opponent attacking/posture/state,
facing quadrants, heading deltas, side flag, guarded body contact,
recovery counters, button tap latches, health/launch and power timer.
Full 0x45+ reversal conditions and the original-EXE-only common rows
are **not yet implemented**. Basic command words and double taps work;
external command sequence tables can now be passed to the VM for
0xC00E–0xC04C / 0xC7FF–0xC827.

The normal diagnostic VM provides no unsupported character-state facts,
so these predicates cannot accidentally invent successful branches.
Production parity requires reconstructing those facts from the original
PS1 combat engine state, importing the full original motion sequences,
and implementing buffering/look-ahead, transitions and frame events.

## PS1 authored attack segments (DIVMOT section 9)

`src/game/attack_records.[ch]` now decodes **40-bit packed X/Y/Z**
coordinates, the 13 known descriptor layout kinds, joint-pair metadata,
active start/end frames, multi-segment moves and swept prior-frame
endpoints. The four-byte attack descriptor pointer is read from move
offset `0x28`; built-in EXE descriptors are identified but **not yet
materialized**. Bad section offsets and truncated records fail without
reading outside the bank. Alternative point blocks for Ogre/Mokujin
have opt-in offsets, but real-ROM visual verification remains pending.

To preserve these original attack records **only in your private local
bank**, use `--with-hitboxes` when compacting *both* the character and
common DIVMOT archives:

```sh
python3 scripts/divmot_to_logic.py \
  --bank assets/imported/tekken3-usa/raw/098.bin \
  --output assets/imported/tekken3-usa/moves \
  --with-hitboxes

python3 scripts/divmot_to_logic.py \
  --bank assets/imported/tekken3-usa/raw/073.bin \
  --output assets/imported/tekken3-usa/moves \
  --with-hitboxes
```

The `t3_move_probe` in a private Saturn build then samples
**local-space attack geometry for the active frame** from each VM's
correct character/common bank. The diagnostics structure (version 3)
records the number of decoded attack segments and failed/truncated
records per fighter, without changing the playable fallback's hit
detection. Segment start points flagged as swept still require the
previous frame's world transform. Hurt-cylinder intersection now exists as an independent C primitive, but
it is **not yet fed original PS1 joint positions**, and gameplay hits,
body spheres and hit/reaction resolution are still pending.

The compactor still omits original animation, camera, texture, audio,
and frame-event sections. The resulting `.t3logic` plus attack
records is an intermediate for research and local builds; **never
publish it or an ELF/ISO containing it**.

## World transforms and hurt cylinders (portable C)

`src/game/psx_collision.[ch]` provides an integer-only, allocation-free
world-space collision geometry layer for the original PS1 coordinate
system:

- Rotates and translates both endpoints of authored baked attack segments
  with caller-supplied **Q12 heading sin/cos** and PS1 move roots. A swept
  start point explicitly uses the **previous frame root**; its rotation and
  translation are not silently substituted with those of the current frame.
- Implements finite **vertical segment-vs-cylinder intersection**:
  clips against the cylinder's vertical slab, then checks the horizontal
  closest point against the radius. Both the geometry and tests use
  64-bit intermediate arithmetic; only an integer Q16 segment parameter
  is approximated. This is **not yet claimed bit-exact** to the MIPS original.
- Builds the standard **14 PS1 hurt cylinders** from the original
  character's **24 world-joint translations** using the documented joint
  lookup, normal/large radius profiles, head −120 and waist −60 Y offsets.
  Some character-specific larger head profiles still require the private
  original EXE table.
- Resolves the first hit zone, preserving attack-segment order, and
  reports invalid/out-of-range coordinates instead of overflowing.

This is a usable collision primitive with synthetic host regressions,
**not yet the authoritative game hit test**. The Saturn frontend still
renders separately, and original hit results **do not alter gameplay**.
The reason is significant: the current static KMD import does not yet
compose authentic animated 24-joint PS1 world matrices. Passing generic
LibSaturn skeleton transforms as if they were PS1 joints would create
false contacts. We must first finish DIVMOT animation import, precise
PS1 root/heading calibration and per-frame joint evaluation.

## Next major work, in dependency order

1. Validate 22 banks and all available row/branch references from the US
   disc (on a private machine with a game copy).
2. Implement all original input sequence tables, branch restrictions and
   condition types (not just condition 0). Add strict unknown-opcode diagnostics.
3. Link character and shared banks into two combat actors; data-driven
   authoritative motion selection instead of four fabricated attack entries.
4. Decode original motion banks into LibSaturn skeleton keyframes and
   frame events; authored attack segments are now parsed, but collision
   against transformed hurt volumes remains unfinished.
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
