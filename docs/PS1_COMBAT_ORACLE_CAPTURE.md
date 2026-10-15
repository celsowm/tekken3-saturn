# Tekken 3 USA: private PS1 combat oracle capture

This is a **real emulator-memory capture pathway**, not a claim that
real retail frames have already been recorded. It does not bundle a PS1 disc,
BIOS, game assets, private trace data, or recompile proprietary MIPS code.

## Version and verified static address provenance

Use **Tekken 3 USA SLUS_004.02**, not Japan Rev.1 and not an arbitrary
patched build. Research source:
[n0madic/Tekken3Remake](https://github.com/n0madic/Tekken3Remake),
[Japan Rev.1 fighter offsets](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/code/fighter.md),
[EXE memory map](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/code/memory-map.md)
and the exact
[USA relocation map](https://github.com/n0madic/Tekken3Remake/blob/master/tools/remake_import/layouts/usa.json).

The research repo's `usa.json` `blocks.exe.points` contains
explicit **[canonical Japan address, original USA source address]**
pairs:

| Description | Japan Rev.1 address | USA SLUS_004.02 address |
|---|---|---|
| Fighter P1 | `0x800A96F0` | `0x800A9228` |
| Fighter P2 | `0x800AAF7C` | `0x800AAAB4` |
| Global frame counter | `0x800AFF14` | `0x800AFA4C` |
| Global game state | `0x800AE6CC` | `0x800AE204` |

Fighter stride is `0x188C`. Structure-relative member offsets are
shared across these versions according to the upstream research:

| Member | Offset | Reading |
|---|---|---|
| Current move slot | `+0x1A0` | little-endian signed 16-bit |
| Current pose frame | `+0x058` | little-endian signed 16-bit |
| Health / health max | `+0x3F4 / +0x3F8` | little-endian signed 32-bit Q16 |
| Fighter state word | `+0x060` | little-endian unsigned 32-bit |
| KO flag | `+0x0DE` | unsigned byte |
| Current move frame | `+0x05C` | signed 16-bit |
| Hit freeze | `+0x076` | signed 16-bit |
| Recovery frame counter | `+0x11A` | signed 16-bit |

The upstream USA executable layout lists SHA-256
`525e689449eb135bf833ecaff26a5e2623686e5a4e2104f425978bb33a55ffec`
for its normalized extracted **EXE block** (`size=1183744` bytes).
**The capture scripts do not fingerprint the disc or EXE**, so the operator
must verify their own executable independently. The mapping has been
checked statically against the upstream layout; it still needs an actual
live original-game capture to confirm timing and field behavior.

## Option A: continuous capture inside PCSX-Redux

Use [PCSX-Redux](https://pcsx-redux.consoledev.net/) with a locally
owned USA game copy. Its Lua API supports `PCSX.getMemPtr()` and
`PCSX.Events.createEventListener("GPU::Vsync", ...)`; see the
[documented memory API](https://pcsx-redux.consoledev.net/Lua/memory-and-registers/)
and [event API](https://pcsx-redux.consoledev.net/Lua/events/).

1. Boot the verified **USA** game. Do not use Japanese addresses.
2. In Debug → Lua editor or Lua console, load the script from your clone:

   ```lua
   assert(loadfile("/path/to/tekken3-saturn/scripts/pcsx_redux_capture_usa.lua"))()
   ```

3. Start a fight normally. The recorder waits for guest game state 8
   (`FightMain`), two valid fighter structs and a new guest frame count.
   It writes two fighter records for each guest frame, ignoring duplicate
   GPU VSync events without advancement.
4. The default output is
   `tekken3-usa-ps1-combat.psx-combat.jsonl` **in the emulator's
   working directory**, up to 1,200 frames. Edit the `CONFIG` table
   near the script's top for another path/limit.
5. To stop from Lua console, run:
   ```lua
   T3_REDUX_CAPTURE.stop("finished")
   ```

Reset, savestate load, leaving the fight or a skipped guest frame ends
the capture rather than quietly combining unrelated frames. The file is
closed on normal stop. The callback stores only small field snapshots,
not the complete game RAM.

**Sampling limitation:** `GPU::Vsync` is an emulator event; it is *not*
by itself proof of the exact PS1 `FightFrame` end boundary. The guest
counter is checked for duplicates/gaps and later frame alignment must
be confirmed by comparing the move/HP transitions against breakpoints
around `HitApply` and `MoveStepAll`. No PS1 input movie, pad timing,
savestate baseline or original replay trace has been supplied here.

## Option B: independent decoding of full RAM snapshots

PCSX-Redux also exposes a read-only debug web endpoint
[`/api/v1/cpu/ram/raw`](https://pcsx-redux.consoledev.net/web_server/).
Enable the web server in emulator settings (normally localhost:8080).
**Pause emulation and capture at a consistent point after each
single-frame advance**. For example, on your own machine:

```sh
mkdir -p /private/tekken3-ram
curl --fail --silent --show-error \
  http://127.0.0.1:8080/api/v1/cpu/ram/raw \
  -o /private/tekken3-ram/00000.bin
# Advance EXACTLY one emulated frame, pause, then save 00001.bin, etc.
python3 scripts/psx_ram_oracle.py \
  --output /private/tekken3-usa.psx-combat.jsonl \
  /private/tekken3-ram/*.bin
```

Each input must be exactly 2,097,152 RAM bytes, `gameState=8`,
both fighters valid, and subsequent guest counters exactly +1
(modulo unsigned-32 wrap). Files are sorted by filename.
Any invalid frame aborts without replacing an existing output.
This decoder is also useful to independently check a Lua-captured
specific frame's unsigned/signed field decoding.

## Trace contract and what NOT to infer

Every tick contains exactly two records:

```json
{"tick":0,"fighter":0,"move_slot":3,"pose_frame":0,"health_q16":32768000,"ko":0,"source_frame":1234,"psx":{"state":2114,"move_frame":1,"hit_freeze":0,"recover_frames":0,"got_hit":0,"guarded":0,"hit_clean":0,"counter_hit":0,"in_air":0,"root_x":0,"root_y":0,"root_z":0,"facing":32768}}
```

This JSON is a **format example**, not a claimed observed frame.
`tick` is zero-based within this clip; `source_frame` preserves the
raw game counter. The `psx` object contains directly observed
struct fields, not synthesized combat state.

Crucially, the recorder **does not invent** `phase`,
`phase_frame`, `frames_left`, `combo_hits_received` or `hit_slot`:
those require separate interpretation, end-of-frame alignment and
independent oracle evidence. The existing
[`compare_psx_combat_traces.py`](../scripts/compare_psx_combat_traces.py)
compares only the known top-level fields, ignoring `source_frame` and
`psx` diagnostics. Other fields on the Saturn side must match the
same moment in a fight, with known identical starting state and inputs.

```sh
python3 scripts/compare_psx_combat_traces.py \
  --ps1 /private/tekken3-usa.psx-combat.jsonl \
  --saturn /private/tekken3-saturn.psx-combat.jsonl
```

A passing synthetic unit test demonstrates format or arithmetic only,
not authentic 1:1 parity. Keep all original retail-derived traces and
executable bytes private and gitignored.

## Report raw frame transitions before assigning original PS1 semantics

Once a **genuine** original-game trace has been captured, run:

```sh
python3 scripts/analyze_psx_combat_oracle.py \
  --ps1 /private/tekken3-usa.psx-combat.jsonl \
  --output /private/tekken3-usa-observations.json
```

This produces a JSON event timeline for both fighters: exact move-slot
changes, Q16 health deltas, KO flag edges, directly observed raw
`got_hit`/`guarded`/`hit_clean`/`counter_hit`/`in_air`/
`hit_freeze`/`recover_frames` changes, and consecutive intervals where
the same move slot and pose frame were recorded. A pose-static interval
**is not automatically hitstun** (it may be intentional holds, guard,
transition freeze, pause or a sampled blend). The script rejects frame
gaps, malformed diagnostic data and inconsistent paired snapshots.

Only after the events have been aligned to PS1 `FightFrame` stages
can frame durations and recovery/knockdown classifications be imported
as oracle-supplied facts into the Saturn MoveVM bridge.
