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

## Saturn runtime motion diagnostic (private)

The optional local-bank Saturn executable now runs `t3_motion_open` and
`t3_motion_sample` at the current DIVMOT move/frame. Its externally
discoverable `g_t3_diagnostics` record **version 4** exposes, for each
fighter, the authored stream's frame count, decoded PS1 root XYZ and
a conservative status:

- `0`: no section-6 stream available; `1`: all 49 channels sampled;
- `2`: root XYZ sampled but other channels require unavailable tables;
- `3`: original spline coefficients are missing;
- `4`: invalid/unsupported source animation.

An additional **private, normalized 0x660-byte spline lookup table file**
may be supplied with `-DT3_LOCAL_PSX_SPLINE_TABLES=/private/tables.bin`.
The build rejects other sizes. This table is *not* included in Git or CI:
CI creates only a synthetic zero-filled test blob.
Do not assume the Java/PS1 executable in another region shares the
Japan Rev.1 virtual addresses. The authenticated USA location must be
determined separately before extraction; this change does **not**
supply or download retail coefficient data.

The diagnostic does not modify gameplay or animate the original model.
No PS1 FK/IK matrices are reconstructed yet.

## Original 49-channel animation streams (section 6)

The new independent `src/game/psx_motion.[ch]` decoder opens a raw
**PS1 DIVMOT animation stream** by the original move row's section-6
offset, including `0x40000000` references into shared `divmot99`.
It validates the 50-byte stream header, the 49 per-channel key counts,
the full signed 16-bit payload, frame duration, and every compressed
B-spline knot span **without reading beyond section 6**.

For any frame in `0..N-1` (clamped), it samples all 49 channels,
including the first 3 root coordinates, four 3-axis IK limb targets,
four limb swivel angles, and ten 3-axis Euler rotations. The original
**odd channels are sampled in reverse time**. Constants (0/1 keys),
linear 2-key channels, and raw one-key-per-frame streams can be
decoded with no auxiliary data. For all other key counts, the decoder
executes the original fixed-point spline arithmetic and requires the
**original 0x660-byte spline/index lookup tables from the user-owned
PS1 executable**, passed in a normalized `t3_motion_tables_t`.
If those tables are unavailable, a call that needs a spline returns
`0` (unavailable), **never a fabricated interpolation**. Invalid
streams return `-1`.

To keep the tagged motion bytes in locally compacted banks:

```sh
python3 scripts/divmot_to_logic.py \
    --bank assets/imported/tekken3-usa/raw/098.bin \
    --output assets/imported/tekken3-usa/moves \
    --with-motion --with-hitboxes

python3 scripts/divmot_to_logic.py \
    --bank assets/imported/tekken3-usa/raw/073.bin \
    --output assets/imported/tekken3-usa/moves \
    --with-motion --with-hitboxes
```

The generated `.t3logic` retains the original **section 6 and 9**
contents along with branches/rows/slots; all other copyrighted
runtime data stays omitted. A synthetic host C test covers shared
stream addressing, key-count bounds, linear/time-reversed sampling,
and a keyed channel with a synthetic spline table. A synthetic Python
test proves both optional sections survive compaction unchanged.

**Important:** decoding the scalar pose vector does **not** compose
PS1 local rotations/world joint matrices, nor perform four-limb IK,
model offset transforms, root scaling, material animation, collision
reactions or original physics. Source animation bytes and compacted
banks remain PRIVATE. Actual USA stream lengths and spline weights
have not been compared with a PS1 oracle in this milestone.

## Original KMD model parent/offset wiring

For an explicitly enabled local KMD, the private asset builder now emits
`t3_private_kmd_parts[22]`: each part's original parent draw-part
index, animation matrix slot and model offset `(x,y,-z)`, taken from
the user's own KMD row data. When the build **also** has a local
DIVMOT bank, valid pose and private sine window, the Saturn diagnostic
composes the partially known skeleton using those real model offsets.
`g_t3_diagnostics` version **6** adds
`psx_fk_valid_model_parts[2]` so emulator harnesses can distinguish
FK-derived joints from unavailable IK-dependent subtrees.

The initial model-space root is identity, **not an approximation of the
original fighter's world orientation**. These transforms are only
for research, not yet rendered or used to decide contacts. The
normal public/procedural build emits no KMD data or joint tables.

## Saturn FK diagnostic (optional, private)

The private Saturn move probe now feeds fully decoded DIVMOT pose vectors
into the Euler FK calculator when a 0x2800-byte private PS1 trigonometric
lookup window is supplied. Pass
`-DT3_LOCAL_PSX_TRIG_TABLE=/private/ps1_sine_window.bin` alongside
`-DT3_LOCAL_DIVMOT`, `-DT3_LOCAL_COMMON_DIVMOT`,
`-DT3_LOCAL_PSX_SPLINE_TABLES`, and optional `T3_LOCAL_PSX_SEQUENCES`.

The externally discoverable `g_t3_diagnostics` **version 6** exposes
`psx_fk_valid_slots[2]` and the root rotation's Q12 XX component for
each fighter. With a valid full pose and lookup window the mask contains
the original ten FK slots; it is zero when missing source data.
This **does not** pretend to solve IK or animate the rendered mesh.
The local builder validates the table length but cannot authenticate
retail bytes; use an original private executable and region-verified
table offsets, never guessed addresses. Public CI only compiles a
synthetic zero-filled table.

## PS1 Euler FK and partial model-joint composition

`src/game/psx_pose.[ch]` now evaluates the **ten PS1 Euler rotations**
stored in pose channels 19..48 into their original 18-slot indices
`[0,1,11,2,3,7,6,10,14,17]`. The implementation follows the GTE
fixed-point arithmetic: 4.12 sine/cosine values, signed saturation at
each GPF multiply, 16-bit angular wrap and the documented rotation
matrix construction. It does not call `sin()`/libm on the SH-2.

The input is an **explicit private 0x2800-byte sine+cosine lookup window**
from the user's PS1 executable (sin table plus 0x800-byte cosine lookup
displacement, including wrap coverage). **Never use guessed USA executable
addresses.** For tests, the window is generated with synthetic cardinal
angles; original trigonometric data is not shipped.

The independent 22-part model composer accepts original PS1
parent-part indices, KMD offsets `(x,y,-z)`, and a world-space root
matrix. It propagates rotation/translation through the parent chain
and **produces a validity bit per part**. Parts whose rotation depends
on the missing IK slots 4,5,8,9,12,13,15,16, and the accessory slots
18..21, are explicitly marked unavailable. This avoids passing invented
joint positions to hitbox/collision code. The module uses no malloc,
floating point, hardware GTE or LibSaturn-specific API.

**Current boundary:** these ten FK slots and generic composition are
tested but do *not* yet constitute the complete original 18-slot
FK/IK pipeline, nor a full 22-part animated fighter. The PS1 sine table,
actual KMD parent offsets, limb IK, world root heading and oracle
validation all remain necessary.

## Four-limb original integer IK (18 rotation slots)

`src/game/psx_ik.[ch]` ports the original PS1
`IkLimbTarget`/`IkSolveLimb` path (based on the independently verified
research implementation in `Tekken3Remake/tools/research/pose.py`).
It preserves the original numerical model: split-15 GTE target
conversion, signed 32-bit wrap, fixed-point Euler basis rotation,
`SquareRoot0` mantissa lookup, swivel angle, arm/leg bend constants
and the special short/overextended limb cases.

In addition to ten FK Euler slots, all eight IK slots are computed:
right arm 4/5, left arm 8/9, right leg 12/13 and left leg 15/16.
A limb with zero length, an unsupported short reach or invalid
lookup coefficients **remains unavailable**; the output mask never
creates a fabricated limb.

A normalized **PRIVATE** IK file is exactly `0x1F2` bytes:

- `0x000..0x17F`: 192 signed 16-bit PS1 SquareRoot0 mantissa values;
- `0x180..0x191`: nine signed Q12 limb-basis entries;
- `0x192..0x1C1`: four little-endian signed XYZ skeleton-offset triples;
- `0x1C2..0x1F1`: four little-endian signed XYZ limb-root-offset triples.

These bytes must come from the user's own PlayStation executable,
with region-specific addresses verified first; Japanese Rev.1
absolute addresses are *not* used to guess USA offsets.
For an intentionally local build provide
`-DT3_LOCAL_PSX_IK_TABLES=/private/psx_ik.bin` as well as
`T3_LOCAL_PSX_TRIG_TABLE`, `T3_LOCAL_DIVMOT` and optionally
`T3_LOCAL_PSX_SPLINE_TABLES`. No retail table bytes are checked in.
`g_t3_diagnostics` **version 7** exposes the additional successfully
computed IK slots in `psx_ik_valid_slots[2]`, and when a local KMD
is available, its composed model-parts validity mask now includes
the IK-supported chains.

The independent C host regression uses **synthetic** square-root
mantissas and cardinal sine values. It exercises overextension,
normal bending, short/singular limbs, determinism and malformed
input. This does **not** establish PS1 bit-exact parity or original
animation-on-screen. Only real-table/real-pose comparisons against
an emulator/MIPS oracle can establish that.

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
