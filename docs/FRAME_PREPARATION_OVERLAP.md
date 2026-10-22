# Prepare the next image during the preceding VDP1 draw

This follows the rejected [compiled renderer prototype](COMPILED_RENDERER_PLAN.md).
The goal remains 60 committed logic ticks and 30 newly completed images per
second. The change below removes an outer synchronization dependency; it does
not reduce the renderer's useful CPU work or repair discarded simulation time.

## Ownership and clock

The old loop calls `sat_wait_vblank()` before preparing any simulation,
geometry or staged commands. The selected `SAT_VDP1_FRAME_ON_SUBMIT` SDK path
also waits for the preceding list's END and settled framebuffer swap inside
`sat_end_frame()`, before uploading the next command/Gouraud tables to VRAM.
Waiting at the start serializes preparation with the preceding presentation.

`T3_OVERLAP_VDP1` allows the next frame's CPU work to start immediately. Its
vertices, painter queue and command staging stay in WRAM. The existing final
submission retains its draw-completion, swap and timeout protection. Original
textures, palette configuration and backdrop are static during this encounter.
No drawing is moved to the Slave and no extra pose snapshot or frame of visual
latency is introduced. Both actor geometry tasks retain their existing split.
One startup wait initializes the first original pose before gameplay becomes
visible; subsequent preparations overlap drawing.

The clock must advance independently of that wait. In the pinned SDK,
`sat_frame_count()` is updated by `sat_wait_vblank()`, rather than by every
VBlank interrupt. With overlap enabled, the port instead reads the public
`sat_irq_count(SAT_IRQ_VBLANK_IN)` for scheduling and diagnostics. If interrupts
are unavailable, it retains the original wait and field counter. The completed
overlap counter counts frames whose initial wait was actually bypassed.

An initial diagnostic that removed the wait without replacing the clock
completed hundreds of submissions with zero simulation ticks. It is invalid
and excluded. The release guard now explicitly requires positive committed
ticks and observed fields, in addition to both original actor poses.

CMake requires complete-list presentation when selecting overlap. The SDK pin
remains `6302bec`; this increment requires no additional SDK runtime change.
Overlap defaults ON for the private original-KMD owned encounter using
complete-list presentation. Other configurations retain their preceding default.
Existing caches preserve an explicitly selected OFF control.
The manual's [VRAM access rules](https://github.com/celsowm/libsaturn/blob/6302bec652ad5ef79da3a7c834ece9340e0e2b38/docs/sega_saturn_hardware/hard/vdp1/hon/p02_10.md)
require protecting uploads from concurrent drawing. That protection stays at
the existing SDK submission boundary, rather than at CPU preparation.

## Measurements

Three alternating final Mednafen comparisons advance FRAME 59 to 657 between
movie seconds 18 and 78. Their controls advance 50 to 557: 598 versus
507 submissions, a **17.95% increase**, from 8.45 to 9.97 submission increments/s.

| Pair | Initial-wait FRAME | Overlap FRAME | Control delta | Overlap delta |
| --- | --- | --- | ---: | ---: |
| 1 | 50 → 557 | 59 → 657 | 507 | 598 |
| 2 | 50 → 557 | 59 → 657 | 507 | 598 |
| 3 | 50 → 557 | 59 → 657 | 507 | 598 |

Neutral input is held in both variants. The faster candidate reaches the normal timeout/next-round sequence
inside this interval; the slower control reaches it later. These are whole-game
submission observations, not 30 verified pictures/s or physical hardware FPS.
The preceding prototype, before the startup wait, measured 597 versus 507.

The first 40 measured seconds remain in the same live first round for both
variants: FRAME 50 → 388 versus 59 → 456, or 338 versus 397 submissions
(**17.46% more**). Thus the gain also appears before the round transition.

Matched cache-on Ymir runs over 5,400 requested fields retain original assets,
combat, camera and qualified SDK. Final endpoints are:

| Observation | Initial wait | Preparation overlaps VDP1 |
| --- | ---: | ---: |
| Observed gameplay fields | 5,372 | 5,374 |
| Committed simulation ticks | 4,133 | 4,610 |
| Completed submission counter | 1,033 | 1,153 |
| Discarded elapsed fields | 1,239 | 764 |
| Late swaps | 163 | 151 |
| Draw waits / timeouts | 2 / 0 | 2 / 0 |
| Guest first error | 0 | 0 |

During exactly 3,600 warmed video transitions at fields 1,800–5,399, the model
crop changes RGB in 695 versus 775 transitions: 11.58 versus 12.92 crop changes
per nominal second. This excludes the FRAME HUD and counts changed crops,
not unique poses or independently verified presentation epochs. It complements
the submission observations without converting them into a hardware FPS claim.

The frozen tick-120 builds match all 97 stable fields and exact model/stage
pixels. The crop SHA-256 remains
`48cdd00660bfecf7c9164266d052121b2986bc9777a9d164f18f93b3aa2fb896`.

The candidate application is 854,416 initialized bytes, below 983,040. High
BSS ends at `0x060f90c8`, retaining the checked 16 KiB Master stack; Low WRAM
ends at `0x0027c820`. No expansion memory or additional geometry buffer is used.

## Validation

All 46 host CTests pass, together with the existing probe, workload and combat
orchestration unit tests. The final startup/clock version also matches every
declared field digest over **5,481 + 5,632 + 5,221 = 16,334 original/host/Saturn
ticks**, including the two knockout traces and drawn-match trace. Those
diagnostic builds move geometry scratch to Low WRAM to fit reference fixtures;
they retain cache emulation and both rendering CPUs. They are correctness
controls, rather than release timing evidence.

The live suite's fixed `KO end + 1,600 fields` checkpoint spent 1,008 logic
ticks of the second round waiting after the first victory in the faster build.
Its scripted second attack sequence then reached timeout with 32 defender HP.
The runner now locates the first observed fresh second round before beginning
those attacks. The first neutral timeout snapshot likewise searches for the
second round, since a faster build can finish both draws before field 6,000.
Both variants run the same revised runner. Every original KO, health, score,
replay, authored-pose, reset and timeout assertion remains in force. No state
is edited and the original game clock is not accelerated by the runner.
The final candidate passes all **24 live scenarios**, including both KOs,
authored replay/victory, match reset and both neutral timeout checkpoints.
The matching control also passes all 24 scenarios with the revised runner.

## Visibility experiments rejected during this follow-up

Two separate preparation changes were implemented, measured and removed from
the working runtime before the overlap experiment:

- Exact world-backface classification before projection reduced the selected
  endpoint from 1,112 projected actor vertices to about 813. It performed more
  geometric-normal tests and added gather/scatter work. Mednafen gives 487
  versus 507 submissions in the matched 60-second interval, a 3.94% regression.
- Compiled rigid-face planes used a conservative inverse-transform and rounding
  interval, preserving ordinary culling for blended, cross-joint, two-sided,
  reflected and ambiguous cases. Of 100,000 synthetic poses, 65,281 proven
  decisions match an independent wide oracle; 30,000 shared-scene queue cases
  and frozen 97-field/pixel comparison also pass. The live endpoint proves
  796 orientations and projects 1,000 vertices, but Mednafen gives 451 versus
  507 submissions, an 11.05% regression. Its classification/publication/storage
  costs outweigh the avoided preparation in this implementation.

The rejected sources and builds are retained privately under
`assets/imported/compiled-visibility-source`, `visibility-first*` and
`compiled-visibility*`. They add no public profiling framework or runtime
options to this increment. Their results do not prove that all early-visibility
renderers are ineffective.

## Retained evidence and remaining budget

Final release builds are `build/vdp1-overlap-qualified` and
`build/vdp1-overlap-qualified-control`. Recordings are
`assets/imported/vdp1-overlap-qualified{-control}-cd-*`; cache-on field/video
observations are `vdp1-overlap-qualified{-control}-5400`. Frozen captures are
`vdp1-overlap-qualified{-control}-frozen`; continuous reference comparisons are
`vdp1-overlap-qualified-reference-{p1,p2,draw}`.
The live reports are `vdp1-overlap-qualified{-control}-combat-adaptive/suite.json`.
A fresh `build/vdp1-overlap-default` selects the new default and reproduces
the qualified ELF, application binary and raw CD **byte for byte**.

`assets/imported/vdp1-overlap-qualified-source` retains the source patches,
release executables/maps/caches, runtime/recording identities and a structured
result. It excludes the invalid zero-tick run and superseded recordings from
its performance comparison.

The overlap removes waiting; the original render-work budget remains above
33.33 ms. The four-field clamp still discards elapsed time. This increment
therefore leaves both the sustained 60-tick requirement and the 30-image
requirement open. A further renderer architecture must remove useful work
and include its full memory/publication cost before another promotion claim.

The subsequent [dense-command and simulation pipeline experiment](COMMAND_PREPARATION_EXPERIMENT.md)
has now tested that proposed Slave preparation window. It fails the architecture's
continuation gate; this qualified overlap release remains the production default.
