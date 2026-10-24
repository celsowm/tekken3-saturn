# Complete-list presentation

The port previously left VDP1 in automatic PTMR=2 drawing. It builds a scene
over several VBlanks and uploads a new command table at submission. Automatic
drawing can therefore traverse that table during its replacement, producing
extra intermediate image changes that are not completed application pictures.

The SDK's existing `SAT_VDP1_FRAME_ON_SUBMIT` policy already solves this
ownership problem for Dino. The port now selects it after `sat_init`, before
the loading screen. It plots the completed command table and uses the SDK's
draw-end/swap handling. `T3_FRAME_ON_SUBMIT` defaults ON; OFF retains the
automatic-draw control. No driver, geometry, texture or combat rule changes.

## Measurement and integration

The new external harness is LibSaturn `1ae028365208b54595832253655537c4891e1840`.
Its Harness build CI passes. The installed runtime archive remains exactly
the same as SDK `10f9956`: the harness increment changes no runtime source.
Rebuilding/installing the package preserves archive SHA-256
`a6c9bb05cf5dee85595a61cb67c8fca6152bd15926d51d8c93819979c3670022`.
All five package checks pass; the package-consumer retry needed the configured
SH-2 tools directory in PATH. This is separate from the older unrelated Phase 3
missing-palette failure, which is not represented as green.

`saturn_probe.py --video-observations` now retains per-field `video.csv`.
`--video-crop 0:61:320:135` selects the model/floor region below the counter HUD.
Repeat `--screenshot-frame FIELD` to inspect exact transitions. The SDK's
`analyze_video_observations.py` validates completeness and counts exact RGB
changes. It compares pixels directly; the fingerprint does not decide equality.

Same source, assets, 128-entry local pose cache, camera, stock RAM and explicit
cache-on Ymir controls, fields 300 through 3900:

| Measurement | Automatic drawing | On submission |
| --- | ---: | ---: |
| Selected interval | 3,600 fields / 60 nominal seconds | Same |
| Model-region RGB transitions | 800 | 672 |
| RGB transitions/s | 13.333 | 11.200 |
| Successful CPU submissions in diagnostic endpoint interval | 672 | 672 |
| Committed ticks in diagnostic endpoint interval | 2,688 | 2,688 |
| Endpoint interval VBlanks | 3,598 | 3,598 |
| Submissions/s from endpoint counters | 11.206 | 11.206 |
| Logic ticks/s from endpoint counters | 44.825 | 44.825 |

The diagnostic publication boundaries differ by two fields from the exact
video interval. Equality of aggregate counts is supporting evidence, not an
individual submission-to-visible-frame identity proof. The old 13.333 figure
is **not FPS**: screenshots at fields 300 and 301 have HUD FRAME 51 and
different model pixels. ON_SUBMIT removes the excess transitions in this
neutral control. This corrects presentation; it does not claim a speed gain.

An independent 12-field ON_SUBMIT execution trace has no dropped records:
over its 11-field endpoint interval it records two draw starts, two ends,
two swaps and two completed-draw visibility increments, rather than the
automatic control's 11 of each. Both observed CPUs have CCR=1. The two sampled
draw spans are 59,334 and 59,459 system cycles, median 0.13223 fields.
The automatic control's shorter spans omit the manual mode's clear polygon;
do not compare them as equivalent draw workloads or infer hardware fill rate.

## Correctness and current budget

Frozen tick 120 matches all 97 stable snapshot fields and exact model-region
RGB pixels between automatic and on-submit modes. The crop SHA-256 remains
`48cdd00660bfecf7c9164266d052121b2986bc9777a9d164f18f93b3aa2fb896`.
The guest authoritative core is unchanged from the fully qualified local-pose
increment; this presentation experiment does not repeat that core comparison.

All 44 host CTests and 31 Saturn tool tests pass; the public firmware rebuild
passes. A real 35-second Mednafen CD boot succeeds, and reviewed second 34
contains both actors, round/time HUD and FRAME 140. This counter is not a
frame-rate measurement. All 24 live combat/KO/replay/victory/reset/timeout
integration cases also pass with the new presentation policy.

Separate 1,200-field stage builds retain full measured work of 79 ms median /
90 ms p95. With simulation frozen, the two-actor renderer still needs 49 ms
median / 50 ms p95: skin 8 ms, placement 3 ms, geometry including join 20 ms,
flush 10 ms. Stage medians are not additive; outer VBlank/begin-frame waits
are excluded. Correct swaps do not make that independent renderer fit 33.33 ms.
The 30-image/60-logic milestone remains open.

Ymir still has the immediate-result divider model. CD boot is independently
verified, but CD-boot CCR and detailed per-stage timing are not yet captured.
Do not assume that injection CPU configuration establishes actual boot state.
The reusable `extract_mednafen_frames.py` inspected the existing recording
without rebooting: second 8 is still BIOS, second 12 shows READY, and the HUD
counter advances from FRAME 25 at second 16 to FRAME 76 at second 24. That
native-emulator observation must remain separate from the faster injected
Ymir cadence. Capture actual CD-boot cache state before attributing the gap.

## Retained private artifacts

- `visible-{auto,on-submit}-{301,3901}/`: exact per-field RGB and diagnostics.
- `visible-{auto,on-submit}-frozen/`: matched-pose state/pixel gate.
- `visible-transition-128/`: old automatic-mode adjacent screenshots.
- `frame-on-submit-debug/`: complete CPU/VDP1 trace and pipeline report.
- `frame-on-submit-stages/`: full and frozen two-actor stage rings.
- `frame-on-submit-combat-suite/`, `frame-on-submit-mednafen/`.
- `frame-on-submit-boot-timeline/`: retained recording timestamps and hashes.

```sh
python3 ../lib-saturn/harness/scripts/analyze_video_observations.py \
  --csv assets/imported/visible-on-submit-3901/video.csv \
  --probe-report assets/imported/visible-on-submit-3901/ymir.json \
  --after-field 300 --out assets/imported/visible-on-submit-3901/video-summary.json
```

## Per-face ordering bias (measured, not ported)

Tekken 3 does not sort fighter faces by exact depth. Each KMD face's flags word
carries a signed ordering-table bias in bits 25–31, and the original links the
face at `Z[a] + Z[b] − bias` with `Z = SZ/32`, masked to 11 bits; for a
double-sided face the NCLIP sign chooses the bias sign
([`3dmk-models.md`](../third_party/Tekken3Remake/docs/research/formats/3dmk-models.md#face-block-faces)).
The bias is authored data, not a rounding detail: one step moves a face one
ordering bucket, which equals two depth units on the averaged corner depth, so
the observed ±2…8 spans up to eight buckets.

The two render meshes this disc uses carry it:

| Mesh | Faces | Biased | Bias values (faces) |
| --- | ---: | ---: | --- |
| Paul `071.bin` | 773 | 127 (16%) | −2 (32), +2 (76), +4 (15), +8 (4) |
| Law `079.bin` | 768 | 248 (32%) | −6 (32), −2 (128), +2 (26), +4 (62) |

The importer keeps the double-sided bit but not the bias
(`scripts/kmd_to_ska.py`, `parse_faces`), and LibSaturn's scene painter orders
by the four-corner average camera depth at 1/4096 world unit with an 8-bit
`pass` in the high key bits. With exact depth, a bias is only observable where
two faces are within a few units — hair, belt, trim and clothing seams.

Not ported yet, for three reasons that must be settled together:

- the bias needs a per-face channel through every submission path (the
  compiled-program fast path, direct projected spans and the general loop),
  because the key is written inside those kernels, not once per frame;
- one PS1 depth unit is not automatically one Saturn world unit: the KMD bind
  is applied with a ×6 model scale, and the camera framing that pairs with it
  has not been shown to be the PS1 view scaled by the same factor;
- there is no original-render oracle. `fight_harness.py` stubs
  `DrawSkinnedPart` and `FighterUploadTextures`, so packet-level reference
  pictures exist for the 2D screens but not for the fighters. Without one,
  applying the bias could only be judged by eye, and it would move layering on
  a third of Law's faces at once.

The exact-depth painter is therefore retained. Qualifying the bias needs a
fighter draw oracle (real `DrawSkinnedPart`, GTE and packet capture) or a
matched original screenshot of a pose whose biased faces overlap; then the
same frozen-pixel and visual gate used for screen culling can decide it.

