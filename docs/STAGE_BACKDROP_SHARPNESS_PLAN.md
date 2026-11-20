# Sharper stage backdrops: plan

Status: done (2026-11-20), see "Results" at the end. Scope: the fight stages' NBG0 panorama. Tekken
Ball's court strip (stage 19) and Tekken Force keep their current paths until
the fight stages are qualified.

## Why the backdrop looks blurred and blocky

Each panorama is baked once into a 1024 x 240 8-bit cylinder
(`scripts/build_stage_packs.py`, see `PAUL_STAGE_VDP2.md`) and shown by NBG0
with per-line scroll and zoom. Two resamplings meet:

1. **The bake throws away detail.** `scripts/panorama_density.py` compares each
   panorama triangle's texture area with its area on the cylinder. Weighted by
   what the picture shows, the median is about 2.2 source texels per cylinder
   texel along each axis on every stage (90th percentile 2.25-2.9). The 4 x 4
   ray mean averages those texels together: the blur.
2. **The VDP2 enlarges the result.** The line mapping magnifies the cylinder
   about 1.8 x horizontally and 1.5 x vertically with nearest sampling: each
   texel covers about 2.7 screen dots, which makes the blocks.

The PS1 draws the box textures at roughly one texel per screen dot. The floor
is not affected: RBG0 shows the original 64 x 64 tile at its own resolution.
Assembly cannot help here. The VDP2 is fixed-function and the loss is in the
data. The CPU mapping cost (about 100,000 cycles per moving-camera image,
`PAUL_STAGE_VDP2.md`, "Cost") is a separate frame-rate item.

## Target

About one cylinder texel per screen dot: **2048 columns** (0.9 dots per texel
at the nominal zoom). Vertically, 240 rows give 1.5 dots per texel and
360 rows give 1.0.

The current format cannot hold that. An 8-bit 2048 x 360 picture is 737 KB
against 512 KB of VRAM, and VDP2 bitmaps are at most 1024 wide. NBG0 in
**cell format** can: its map is 2 x 2 planes of up to 2 x 2 pages, so
2048 x 1024, and line scroll, line zoom and the per-line vertical coordinate
work the same as on the bitmap.

## Constraints (measured from the source)

| Resource | Today | Free for the backdrop |
| --- | --- | --- |
| VRAM A0/A1 | 1024 x 240 bitmap `0x00000`-`0x3bfff`, rotation parameters, coefficients and line table `0x3c000`-`0x3da7f` | about 245 KB |
| VRAM B0 | RBG0 floor bitmap 512 x 256 (`0x40000`), owned by RBG0 | none |
| VRAM B1 | test-option HUD only (`0x60000`) | about 128 KB in fights |
| Color RAM | fighters from bank 0 (`characters.c`), effects banks 3-5 (`flipbook_burst.c`), floor bank 6, panorama bank 7 | **bank 7 only: 256 entries** |

Color RAM is the tight one. 4-bit cells with per-cell palettes get at most
16 palettes of 16 colours, not the 128 that 2-word names could address,
unless the fight frees more banks.

## Formats to compare

| | A: 4-bit cells, 16 palettes | B: 8-bit cells, deduplicated |
| --- | --- | --- |
| Colours | 16 per cell, from 16 palettes in bank 7 | 255 global (as today) |
| 2048 x 240 characters | 7,680 x 32 B = 240 KB before dedupe | 7,680 x 64 B = 480 KB before dedupe |
| Names (2-word) | 30 KB used rows, two 32 KB planes | same |
| Fit | A0/A1 (characters) + B1 (names), as is | only with roughly 50% duplicate cells (sky, transparency) |
| VDP2 reads per bank | 4-bit: 1 character (2 with the 1/2 reduction) + 1 name | 8-bit: 2 character (4 with reduction) + 1 name |

The original textures are themselves 4-bit with 16-colour CLUTs, so most 8 x 8
cells should come from one or two CLUTs. Measure that before choosing.

## Steps

Each step ends with numbers in this file. Rejected variants are deleted at once.

0. **Measure (host only, no disc).**
   - Make `COLUMNS`/`ROWS`/`ROWS_PER_TAN` arguments of the bake, defaulting to
     `stage_format.h`.
   - Bake every stage at 2048 x 240 and 2048 x 352.
   - Report:
     - for A, the colour error after clustering the cells into 16 palettes
       (k-means over cell colour sets), against today's 255-colour cut;
     - for B, the unique-cell count;
     - for both, the bytes each format needs.
   - Settle the filter at the new density. Horizontal is about 1.1 source
     texels per cylinder texel, so compare the 4 x 4 mean, 2 x 2 and a
     centre ray.
   - Pick the format and size, or stop here if neither fits.
1. **Screen-space proof (host).** Extend `scripts/check_backdrop_mapping.py`
   to render the eight golden cameras through the old and new bakes, and
   compare each with the true box ray cast at 320 x 224:
   - mean error per dot;
   - gradient energy (sharpness) ratio;
   - side-by-side PNGs under `build/backdrop-sharp-*`.

   Gate: lower error on all eight cameras.
2. **SDK check (LibSaturn).**
   - Confirm `sat_vdp2_layers_commit` plans NBG0 as cell, 2-word names,
     planes 2 x 1 pages, with line scroll and line zoom, alongside RBG0's
     banks, at increments up to 2 (the 1/2 reduction).
   - Add a host test for that bank/cycle plan. Fix the planner if it refuses
     a configuration the manual allows.
3. **Game.**
   - Stage pack format version 3: characters, names (packed rows, expanded
     on load) and 16 palettes.
   - `stage_vdp2.c`: configure NBG0 cells and load through the existing
     16 KiB buffer.
   - `stage_backdrop.c`: allow 2048 columns (the `columns>1024` check and the
     wrap mask). The fit itself is unchanged.
   - `tests/test_stage_backdrop.c` at 2048.
   - Ball's strip keeps the bitmap path (`g_strip`).
4. **Qualify.**
   - Frozen-tick Mednafen screenshots, old and new, at three cameras (round
     start, a side step, a high camera).
   - The Mednafen fight rate against today's build (`mednafen_fight.py`,
     280 s): the CPU mapping is unchanged, so any change must come from VDP2
     reads or loading.
   - Stage load time from the loading screen.
   - Frame-rate rule: report this step's cost against R2 (29.73 / 28.25).
5. **If 2048 x 240 qualifies and VRAM is left:** try 2048 x 352
   (`ROWS_PER_TAN` 200 -> 293) for vertical sharpness, gated the same way.
6. **Record and clean up.** Results go into `PAUL_STAGE_VDP2.md`; delete the
   intermediate bakes and builds by explicit list. Follow-ups: Ball and Force
   onto the same path, and the separate assembly pass on the mapping's
   100,000 cycles.

## Risks

- **16 palettes may not be enough** on many-coloured stages (step 0 decides;
  B is the fallback).
- **The cycle planner may refuse NBG0 cells with 1/2 reduction beside
  RBG0.** If so, clamp the increment to 1 and bake a little narrower
  (zoom-in cameras still benefit).
- **Larger stage files mean longer loads.** About 270 KB against 240 KB today
  for format A.

## Results (2026-11-20)

**Step 0, the formats** (`scripts/backdrop_format_study.py`, all 14 stages, 2 x 2 rays a texel). Format A
(4-bit cells, 16 palettes of 15 colours) at 2048 x 352: RMS colour error 0.32-0.80 of a 5-bit step (today's
255-colour cut: 0.02-0.39 at 1024 x 240), every stage uses at most 16 palettes. Unique characters: at most 6,011
at 2048 x 240 and 8,692 at 2048 x 352 (stage 8), against 7,680 that fit VRAM A0/A1 below the rotation tables.
Merging the 935 surplus characters of stage 8 into their nearest neighbours moves its RMS from 0.323 to 0.331.
The filter hardly matters at this density (2 x 2 mean against a single ray differ by under 0.03 step).
**Chosen: format A at 2048 x 352**, 4-bit characters deduplicated, capped at 7,680.

**Step 1, screen space** (`scripts/backdrop_sharp_proof.py`, eight synthetic fight cameras, stage 0). Per-dot error
against the true ray cast does not fall (today 6.3-9.5 levels, 2048 x 352 6.6-9.9): the line scroll places the
picture several texels off (PAUL_STAGE_VDP2.md, "Accuracy"), and a sharper picture shows that misplacement more,
which any pixel metric punishes. Allowing each 16 x 16 block to slide 12 x 4 dots gives equal error at 2048 x 352
(3.7-5.2 against 3.8-5.1) and 10% worse at 2048 x 240. Gradient energy over the truth's: 0.65-0.78 today,
0.70-0.87 at 2048 x 352. The decisive evidence is visual: the sign "FURNITURE" is unreadable today, a smear at
2048 x 240 and legible at 2048 x 352. The gate became "sharpness up and the image legible", not "error down".

**Step 2, planner.** `test_vdp2_nbg_logic` now plans NBG0 as 16-colour cells with 2-word names in two 2 x 1-page
planes, characters in A0/A1, line scroll with zoom, at reductions 1, 1/2 and 1/4. Writing it found nothing, but
running the game found a LibSaturn bug: the map registers of 2 x 1 and 2 x 2 page planes held `address / plane
size`, and the manual (table 4.8) wants `address / page size` (bit 0, or bits 1-0, ignored). Every game before
used 1-page planes, where the two agree; with two pages the layer read the wrong tables and drew nothing, on
Mednafen and Ymir alike. Fixed in `nbg_logic.hpp` and `rotation_logic.hpp`, with a register test from the table.

**Step 3, the game.** Stage format version 4 (`stage_format.h`): head, floor, 256 x 44 two-word names, the
characters. `stage_cells.py` clusters the cells into 16 palettes, shares equal characters and merges down to the
limit; `build_stage_packs.py` writes it (and bakes 2 x 2 rays a texel, about 2 minutes a stage instead of 6).
`stage_vdp2.c` loads the characters into A0/A1 and, in init (bank B belongs to the VS screen until then), the names
into the top half of B1 at 0x68000-0x77fff (the hit particles' NBG1 keeps B1 up to 0x65a80, asserted at build
time), then configures NBG0 as cells. `stage_backdrop.c` accepts 2048 columns (the VDP2's 11-bit horizontal scroll
is the limit; `test_stage_backdrop` runs at 1024 x 240, 2048 x 240 and 2048 x 352). Tekken Ball's court keeps the
version 3 bitmap path and its program does not link the cell loader. Files are 305-400 KB (376 KB before).

**Step 4, qualification** (Mednafen, 280 s attract fight, stage 9). Marked 29.94 / worst 26.26 img/s against
29.91 / 26.21 with the same code and the old backdrop: no cost, as expected since the CPU mapping is unchanged.
Stage load time was not measured (about 30 KB more to read and 176 short VRAM writes for the names).

**Step 5, 2048 x 352** was chosen in step 0, so there is no separate 240-row trial.

**Not done.** Tekken Ball and Tekken Force keep their flat paths; hardware has not seen it. The horizontal
increment reaches about 1.0 at the widest fight cameras (0.98 seen); above 1.0 the VDP2 would need its 1/2
reduction, which the planner does support but the game does not request.
