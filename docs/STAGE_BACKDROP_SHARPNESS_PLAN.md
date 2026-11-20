# Sharper stage backdrops: plan

Status: planned (2026-11-19). Scope: the fight stages' NBG0 panorama. Tekken
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
