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

## Private PS1 IK lookup export from an original executable

`scripts/extract_psx_ik_tables.py` assembles the normalized PS1
lookup images from an original **local** `SLUS_004.02` without
hand-editing binary files. Supply five **verified USA PS-X EXE file
offsets** (not virtual addresses), obtained from the actual USA
executable's disassembly. The tool deliberately has no automatic
Japanese-to-USA address assumption. It checks the supported USA
executable SHA-256, bounds, region overlap, Q12 sine zero-point,
positive SquareRoot0 mantissas, basis and offset magnitude.

```sh
python3 scripts/extract_psx_ik_tables.py \
    --exe assets/imported/tekken3-usa/SLUS_004.02 \
    --sine-offset 0xYOUR_VERIFIED_USA_SINE_OFFSET \
    --sqrt-offset 0xYOUR_VERIFIED_USA_SQRT_OFFSET \
    --basis-offset 0xYOUR_VERIFIED_USA_BASIS_OFFSET \
    --skeleton-offset 0xYOUR_VERIFIED_USA_SKELETON_OFFSET \
    --limb-offset 0xYOUR_VERIFIED_USA_LIMB_OFFSET \
    --output assets/imported/tekken3-usa/ik
```

The arguments above are placeholders, not known offsets: the
extractor will not run until the correct file offsets are supplied.
It writes private `psx_trig.bin` (0x2800 bytes),
`psx_ik.bin` (0x1F2 bytes), and a SHA-256 manifest.
Configure the local build with
`-DT3_LOCAL_PSX_TRIG_TABLE=/private/psx_trig.bin` and
`-DT3_LOCAL_PSX_IK_TABLES=/private/psx_ik.bin`.
These outputs **must remain gitignored and must not enter public CI
artifacts or releases**.

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

## Opt-in LIVE PS1 KMD skin animation preview (experimental)

The new `src/saturn/psx_skin_bridge.[ch]` converts validated PS1
model-space part matrices (Q12) to LibSaturn's row-major 16.16 world
joint matrices, including exact integer rescaling of KMD translations
(`PS1 / 4096`). The original KMD converter already stores skin
anchors in each bone's joint-local coordinates, so
`sat_skin_update_mesh()` accepts these matrices directly without an
extra inverse-bind multiply.

**For an explicitly private build only**, configure the existing
KMD, DIVMOT, sine/spline, and IK table paths and additionally pass:

```sh
cmake --preset saturn \
  -DLIBSATURN_SOURCE_DIR=/path/to/libsaturn \
  -DT3_LOCAL_KMD=/private/fighter.kmd \
  -DT3_LOCAL_DIVMOT=/private/fighter.t3logic \
  -DT3_LOCAL_COMMON_DIVMOT=/private/divmot99.t3logic \
  -DT3_LOCAL_PSX_TRIG_TABLE=/private/psx_trig.bin \
  -DT3_LOCAL_PSX_IK_TABLES=/private/psx_ik.bin \
  -DT3_LOCAL_PSX_SPLINE_TABLES=/private/psx_splines.bin \
  -DT3_ENABLE_PSX_SKIN_PREVIEW=ON
cmake --build --preset saturn
```

This switch is **OFF by default** and cannot be enabled unless the
private KMD, move bank, sine and IK table inputs are configured.
It composes the model's first **18 visible body parts**, and only
overrides the renderer's matrices when every part has an available,
validated transform. Invalid, partial and out-of-range poses leave
the complete previous LibSaturn animation path untouched; the final
four costume accessory parts are not part of the current private
KMD geometry importer.

The new `g_t3_diagnostics` **version 8** exposes
`psx_skin_preview_applied[2]` so an emulator debugger can confirm
whether the original pose actually reached the skinning path.
The preview runs from the DIVMOT *diagnostic move interpreter* and
therefore **does not yet guarantee an original move sequence or
bit-for-bit orientation**: PS1 root scale, facing transforms and the
original model's handedness must still be compared with the PS1
oracle. No damage, collisions or original gameplay reactions are
altered. Each fighter currently loads the same private character
bank, even if different source characters should eventually be
selected. No original data may be released publicly.

Synthetic CI exercises the Q12-to-Q16 transformation, absent or
partial joints and atomic failure behavior, plus strict C11
compilation of the combined KMD+DIVMOT+preview path.

## Animated hurt cylinders and continuous authored attack sweeps

`src/game/psx_contact.[ch]` connects the two previously separate
original-data geometry layers, entirely in portable C (no memory
allocation, LibSaturn dependency or floating point).

**Hurt volumes:** the game's 14-body-zone lookup refers exclusively
to indices `0..17` of the PS1's 24 joint blocks: the first 18 KMD
draw parts. The six extra blocks are attachments/weapon effect
storage. Given 18 validated joint translations, the new adapter
creates the exact index/radius/height-offset layout from
`t3_hurt_from_psx_joints`; absent partial limbs invalidate the
entire frame, rather than substituting bogus joint positions.

**Attack continuity:** authored DIVMOT layouts 1,2,4,6,7 use the
**previous frame's actual world-space segment-0 END**; layouts 2,3,9
do the same for segment 1. Rotating an old local point is not
equivalent when the fighter's root or facing changed. The new
`t3_psx_sweep_sample` records previous endpoints with the move slot
and frame number. Missing continuity or a switch of move fails
closed. Frame-zero anchor points still require the caller's known
previous root.

**Geometric contact:** `t3_psx_contact_probe` returns the first
intersecting authored segment and defensive zone, or a verified
miss when the pose and attack occupy the same world reference frame.
This primitive **does not apply damage, guard, juggle, knockback,
invulnerability, cooldown or 1-hit-per-move state**. Synthetic
regressions test hits/misses, missing IK poses, stale move slots,
missing frame history and first-frame anchoring.

The Saturn research probe now generates each fighter's **model-local**
hurt volumes when its KMD+DIVMOT pose is complete. External diagnostic
version **9** adds `psx_hurt_zones_ready[2]` and
`psx_hurt_head_local_y[2]`. **It does not automatically run live
contact checks** between the two demo fighters: their fallback Q8
positions are not yet calibrated as genuine PS1 fighter roots and
heading angles. Claiming hits by overlaying their local origins
would be misleading and change gameplay incorrectly.

## Original PS1 root motion, facing and KMD scale

`src/game/psx_root.[ch]` now implements the original
`FighterSkeleton.root_displacement` and root placement formula:
the first 3 DIVMOT channels are multiplied by the character's
`scale_percent * 4096 / 100` in PS1 Q12, rounded downward
and **wrapped to signed 16-bit**. Facing controls the rotation of the
root displacement into the arena; heading controls the Q12 model
root rotation (`heading - 0x8000`). All calculations use integers,
the caller's *original PS1* lookup window and bounded world positions.

For private KMD builds, the generated character header now embeds
`T3_KMD_SCALE_PERCENT`, and the move debugger composes its model
joints with an **authored, scaled root displacement**. The debug
preview deliberately uses a zero arena anchor and neutral facing
and heading, so the fallback Saturn fighter's Q8 X/Z cannot be
confused with the authentic PS1 world origin.

A standalone regression checks 100% and 50% KMD scale, sign and
16-bit wrap, 90-degree facing and heading, invalid ranges, and
atomic failure. The emulator will need **actual PS1 world anchor and
facing/heading**, character-specific scale and a matched opponent
pose before these model-space world joints can drive live hit
classification or damage.

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

## Private PS1/Saturn frame-by-frame pose oracle harness

`src/game/psx_trace.[ch]` provides a deterministic **564-byte LE**
record for each fighter and simulation tick. It serializes `tick`,
`fighter`, `move_slot`, `pose_frame`, an 18-bit availability mask,
and the PS1 KMD draw-part order `0..17`. Each joint holds three signed
32-bit model-space/world-space integers and nine signed 16-bit Q12
rotation entries. Unavailable joints are entirely zeroed. This format
is **identical on x86 and the big-endian SH-2**. The encoder leaves the
output unchanged if any validated input or output bound fails.

The `scripts/compare_psx_pose_traces.py` comparator accepts
concatenated 564-byte Saturn records and **independently captured**
original PS1 JSONL records with this structure:

```json
{"tick":0,"fighter":0,"slot":3,"frame":0,"mask":1,"parts":[{"origin":[0,0,0],"rotation":[4096,0,0,0,4096,0,0,0,4096]},null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]}
```

The example above intentionally shows only one available joint. In
strict mode the comparator **requires all 18**, matching ticks,
fighter, move slot and frame. It reports the first 20 divergences,
the number of compared joints, missing frames, and mismatched validity
masks. Differences are in original integer PS1/Q12 units, **not
floating point**. With `--allow-incomplete`, partially known joints
can be inspected without claiming overall parity.

```sh
python3 scripts/compare_psx_pose_traces.py \
    --saturn /private/saturn_pose_frames.bin \
    --dump-jsonl > /private/saturn_pose_frames.jsonl

python3 scripts/compare_psx_pose_traces.py \
    --saturn /private/saturn_pose_frames.bin \
    --oracle /private/original_ps1_pose_frames.jsonl
```

Original PS1 oracle records must be captured independently on a local
emulator; no original game traces or disc assets are included in Git.
### Capturing Saturn binary snapshots

In a PRIVATE `T3_LOCAL_DIVMOT` build, the native SH-2 ELF now exports:

- `g_t3_pose_trace[2][564]`: two consecutive standardized binary
  records, fighter 0 then fighter 1, updated every simulation tick
- `g_t3_pose_trace_epoch`: an incrementing even/odd write guard;
  **even and unchanged** before/after reading both records means
  the pair was captured atomically

An emulator/debugger harness must repeat these operations **once per
simulation tick**: step to the `t3_move_probe_tick` return, read the
epoch, discard odd epochs, read exactly **1128 bytes** beginning at
`g_t3_pose_trace`, reread the epoch and append the 1128 bytes only if
both epochs match. Retry on change. Capturing only one frame from a
running emulator cannot reconstruct missed frames, so do not infer
parity if tick numbers skip.

The binary memory buffer is intentionally **not exported in default
public/procedural builds**. If original KMD matrices are incomplete,
the header still contains the move/frame/tick, but the joint mask is
zero: the tool cannot interpret an old pose as valid. A user-provided
PS1 oracle should use the **same animation tick/input stream**, fighter
index, move slot and PS1 orientation basis before comparison. Without
independently captured PS1 input/frame traces, the tool can only
verify its own synthetic fixtures, not 1:1 fidelity.

A failing comparison is a discrepancy to investigate, **not proof the
original game is wrong**. No reference traces were captured during
this change.

## Independent 18-slot FK/IK numerical differential harness

For genuine **frame-by-frame validation** before Saturn visual testing,
`tools/psx_pose_eval.c` runs the portable PS1 solver on the host CPU
for every packed 49-channel signed-16-bit pose and returns all 18
Q12 rotation matrices with an explicit missing-slot mask.

`scripts/diff_psx_pose_solver.py` compares each computed element
against the **separately maintained** Python
`pose_build_matrices` implementation in the project's pinned
`third_party/Tekken3Remake/tools/research/pose.py` submodule.
The reference source is loaded dynamically rather than copied into
our repository. Both engines receive identical lookup data through
a normalized-table adapter (no assumption about US/JP EXE addresses).

For a private comparison, build the host C runner, supply normalized
trig/IK tables from your own executable and a sequence of signed
16-bit pose vectors (49 entries per frame, little-endian):

```sh
git submodule update --init third_party/Tekken3Remake
cmake --preset host
cmake --build --preset host --target t3_pose_eval
python3 scripts/diff_psx_pose_solver.py \
  --runner build/host/t3_pose_eval \
  --trig /private/psx_trig.bin \
  --ik /private/psx_ik.bin \
  --poses /private/pose_samples_le16.bin \
  --strict-complete
```

The executable path depends on the local CMake host preset output
directory. `--strict-complete` fails when a limb is unavailable,
rather than silently calling the other joints a complete match.
Output includes the first 30 differing elements, original PS1
matrix slot, component index and both signed values. Exit status
0 means every **evaluated** slot matched the reference.

CI generates only **synthetic sine, sqrt and pose input values**
and loads the pinned research submodule for a 16-frame independent
regression. No retail tables, poses, animation banks or PS1 binary
are embedded in the repo or CI artifacts.

**Scope:** this is parity against the published reverse-engineering
reference, **not direct proof of correctness against the executing
PlayStation game**. Subsequent work must capture genuine USA MIPS
pose oracles and confirm root placement, animation blending,
rounding and fighting state.

## Export real private DIVMOT poses into the FK/IK comparator

`tools/psx_motion_export.c` is a **host-only** command-line frontend
for the existing portable C DIVMOT decoder. It resolves the selected
character's move slot (including shared `divmot99` motion
references) and samples the exact requested section-6 frame range
to the 49×signed-16-bit **little-endian** input format used by
`t3_pose_eval`. It fails without writing a partial output when
there is no motion stream, a frame is out of range, a source bank is
malformed or a required spline table is unavailable.

After privately compacting the original move banks with
`scripts/divmot_to_logic.py --with-motion --with-hitboxes`,
compile the native CLI and export a real input batch:

```sh
cmake --preset host
cmake --build --preset host --target t3_motion_export t3_pose_eval
build/host/t3_motion_export \
  /private/bank_0.t3logic \
  /private/bank_99.t3logic \
  /private/spline_lookup_660.bin \
  3 0 0 /private/pose_samples_le16.bin

python3 scripts/diff_psx_pose_solver.py \
  --runner build/host/t3_pose_eval \
  --upstream third_party/Tekken3Remake/tools/research/pose.py \
  --trig /private/psx_trig.bin \
  --ik /private/psx_ik.bin \
  --poses /private/pose_samples_le16.bin \
  --strict-complete
```

The command arguments are, in order: private character bank, optional
common bank or `-`, optional private normalized spline tables or
`-`, move slot, starting pose frame, number of frames (`0` means
all remaining), and destination file. Use the correct character bank
and slot for the chosen fighter. The output and any real test reports
are **private game-derived material**, excluded from the repository.
This end-to-end process still compares with the upstream *research
implementation*, not a live PS1 emulator oracle.

The host CI regression generates a synthetic four-frame animation and
tests exact sampling, an alternate common-bank move, frame windows,
corrupt inputs and fail-closed behavior without shipping game data.


## Batch private DIVMOT FK/IK coverage audit

Run multiple **real locally extracted** move slots without hand-selecting
or writing pose samples to persistent storage:

```sh
cmake --preset host
cmake --build --preset host --target t3_motion_export t3_pose_eval
git submodule update --init third_party/Tekken3Remake
python3 scripts/batch_psx_pose_oracle.py \
  --bank /private/bank_0.t3logic \
  --common /private/bank_99.t3logic \
  --spline /private/spline_lookup_660.bin \
  --trig /private/psx_trig.bin \
  --ik /private/psx_ik.bin \
  --max-slots 48 --max-frames 32 \
  --strict-complete --fail-on-skip \
  --report /private/divmot_fk_ik_coverage.json
```

The optional `--slots 0-40,100,230-260` restricts the search, while
`--max-slots` (1–128) controls how many **unique animation streams**
are sampled evenly across the candidate range, rather than checking
only low-numbered moves. A bank's 4,023 slots are checked for valid
move references and section-6 headers. Multiple slots using the same
bank/offset are counted but **not retested**. If a slot references a
shared animation in `divmot99`, the exporter resolves it through that
bank. `--max-frames` limits the sampled frames per unique animation.
The program collects total frames/components, match/mismatch counts,
invalid/missing stream reasons, first per-slot matrix divergences and
SHA-256 identifiers of the private source banks **and coefficient tables**,
so a divergence can be reproduced with the same private inputs.

All intermediate original 49-channel poses are written only to a
temporary directory and erased afterwards; the JSON report contains
**comparison metadata**, not motion frames, models or source tables.
No retail game files, coefficients or reports are uploaded.

Missing/corrupt streams, unrecognized refs, unavailable spline
coefficients and invalid frame windows are **listed explicitly** rather
than silently treated as a successful match. The default audit
passes if there is at least one fully compared component and no
numerical mismatch; `--fail-on-skip` also fails when one of the
selected animations could not be compared (discovery exclusions
remain itemized). `--strict-complete` fails if any tested frame lacks
a complete 18-slot solver result. A report for synthetic CI frames
does **not** establish bit-exact parity with the original MIPS game.


## Two-fighter PS1 shadow collision observer (no gameplay mutation)

`src/game/psx_shadow_combat.[ch]` now evaluates **both attacking
fighters in the same simulation tick** using the original authored
section-9 attack segments, exact previous-world-endpoint continuity
from `t3_psx_sweep_sample`, and all 14 original-layout hurt cylinders
computed from KMD part positions.

The caller must provide each original model-root translation and Q12
orientation explicitly. The observer does **not guess a PS1 fighter's
world transform** from the unrelated Saturn prototype's Q8 gameplay.
Local joint poses are transformed into the caller's common world
coordinate system before the narrow-phase test; missing poses or
previous-frame sweep endpoints return `MISSING`, and invalid numeric
input returns `BAD`, never a false confirmed contact.

Both directions are independently evaluated from an immutable frame:
simultaneous overlaps are preserved, and per-fighter counters distinguish
frames with geometric overlap from *newly started contact intervals*.
These counters are not hit counts or original Tekken combo semantics.
An authored move switch resets the interval latch; inactive frames
discard stale sweep history. The observer does not change fighter
health, reaction, throw, guard, pushbox, stun, score, or match state.

Host tests cover trade hits, moving/rotated roots, unavailable joint
masks, malformed input, continuity of swept segments and contact edges.
This layer remains **diagnostic-only** until actual USA PS1 world roots,
hit-apply events and animations have been compared against an emulator
oracle.


### Opt-in Saturn shadow collision telemetry with external PS1 roots

The private Saturn build can now run `t3_psx_shadow_step`
after the two DIVMOT interpreters advance. Enable it **only** when
a local KMD, character DIVMOT bank, verified original Q12 trig and IK
tables are already provided:

```sh
cmake --preset saturn \
  -DT3_LOCAL_KMD=/private/character.kmd \
  -DT3_LOCAL_DIVMOT=/private/character.t3logic \
  -DT3_LOCAL_COMMON_DIVMOT=/private/divmot99.t3logic \
  -DT3_LOCAL_PSX_TRIG_TABLE=/private/psx_trig.bin \
  -DT3_LOCAL_PSX_IK_TABLES=/private/psx_ik.bin \
  -DT3_LOCAL_PSX_SPLINE_TABLES=/private/psx_splines.bin \
  -DT3_ENABLE_PSX_SHADOW_CONTACT=ON
```

This *research-only* switch defaults **OFF**. The scene's
approximate Q8 physics is **NOT** substituted for actual PS1 world
placements; instead a debugger/harness must write all three SH-2
symbols exported by the ELF:

- `g_t3_shadow_root_epoch`: 32-bit **native big-endian** seqlock;
  write an odd value while updating inputs and a new even value when
  the pair of roots is coherent
- `g_t3_shadow_external_roots[2]`: two native SH-2 structs, each
  `{int32 x,y,z; int16 sin_q12,cos_q12}`, 16 bytes each;
  both transforms are **original PS1-comparable world-space** values
- `g_t3_shadow_root_valid_mask`: native big-endian 32-bit bitmask,
  **3** means both fighter roots have been provided; 0 means unavailable

The move loop checks that the even epoch did not change while it read
both roots and their mask **and that its value differs from the last
consumed input epoch**. Debugger writers must publish an entirely new
root pair **for every simulation tick**; repeating an old root epoch,
missing a tick during catch-up, or changing it mid-read clears the
swept-endpoint history and reports missing data instead of asserting
a contact. This prevents a paused debugger or stale external oracle
from creating fictitious overlapping contact intervals. It does **not** force an interpreter move, reset
the prototype match's HP, or modify block/throw/KO logic.

`g_t3_diagnostics` **version 10** adds
`psx_shadow_contact_status[2]` (0 inactive, 1 tested miss,
2 geometric overlap, 3 source/history missing, 4 malformed),
`psx_shadow_contact_zone[2]` (-1 or the 0–13 original hurt zone),
`psx_shadow_contact_edges[2]` (rising geometric contacts), and
`psx_shadow_overlap_frames[2]`. The root symbol input is intentionally
not a user-facing public build option: it must be synchronized with
the same DIVMOT moves and animation frames as the local model.

This is an experimental **world-space geometric overlap** only.
Correct original fighter root transforms still need external PS1
state extraction and region-specific calibration. An overlap is not
proof of a successful PS1 hit, damage value or correct reaction.
