# Runtime experiments after stage isolation

The intermediate 30-render/60-logic gate remains open. None of these experiments
establishes a substantial full-game speed gain or a physical hardware result.
The final retained library change is LibSaturn
`a46598b24ddd386ee73cadbd3eebf42a811ea082`.

## Selection and rejected changes

A fresh six-field Ymir profile at fields 360-365 reconciles both CPU histograms
with no dropped records (`recovery-next-hotspots`). Master exclusive modeled
execution costs include skin 8.11%, projection 7.13%, geometric visibility
5.59%, VDP1 sprite push 5.00% and branch search 4.60%. More than 67% of Slave
cost is polling/executor bookkeeping in that window. This identifies idle
capacity; it is not a recoverable whole-game percentage or inclusive timing.

The checked-in Saturn overview's CPU section documents the small 4 KiB SH-2
cache and multiplier/divider. The VDP1 address-map section documents VRAM
arbitration and recommends accessing it outside drawing. The existing SDK
handoff study already rejected DMA for this workload. These constrain further
experiments; neither DMA nor removing draw/swap protections was adopted here.

| Experiment | Measured result | Decision |
| --- | --- | --- |
| Bundle second-actor skinning with its geometry task | Frozen work 65 -> 61 ms; full work 109 -> 106 ms median | Removed; insufficient return for the added adapter and cache ownership |
| High WRAM for match/config/motion cache | Simulation-only 12.08 -> 11.94 ms/tick | Removed; insufficient benefit for extra High WRAM usage |
| Decode branch targets only after eligibility | Simulation-only 11.75 ms/tick | Removed; insufficient benefit to retain an authoritative-core change |
| Q12 coefficient cancellation with Q15 anchors | Frozen skin 12-14 ms, versus 10 ms baseline; integer comparisons passed | Removed; arithmetic equivalence alone does not establish SH-2 speed |
| Constant Q15 anchor conversion | Frozen skin 10 -> 8 ms median/p95 | Retained as a small library arithmetic improvement |

Rejected source patches remain private; none of the additional actor adapter,
High WRAM policy or branch evaluator changes ships. Their private evidence is
`recovery-actor-pipeline-matrix`, `recovery-fast-logic-matrix`,
`recovery-lazy-branch-matrix`, `recovery-q12-skin-matrix`,
`recovery-q12-shift-matrix` and `recovery-q12-cache-matrix`.

## Retained conversion and final measurements

The compiler generated three variable-shift helper calls per skin influence.
Using a constant `q * 2` when `pos_shift == 15` removes those calls and preserves
the same full-width dot products and rounding. Other precisions retain their
generic conversion. Independent integer host comparisons cover 128 fixtures,
all 16 precisions, signed extrema, large coefficients, rigid vertices,
one/two/three influences, unequal and half weights, and split ranges.

The final instrumented 1200-field run after field 300 gives frozen two-actor
work median/p95 64/65 ms, versus 65/66 ms before; skin is 8/8 ms, geometry
27 ms and flush 17 ms median. Full neutral work is 108/121 ms, versus 109/122;
simulation is 12.05 ms/tick. Stage medians are not additive frame samples.
Evidence: `recovery-q15-final-matrix`.

Final untraced endpoints (`recovery-q15-final-{600,1200}`) are
(588 VBlanks, 320 logic ticks, 79 submissions) and (1182, 644, 160).
The interval gives **8.18 submissions/s and 32.73 logic ticks/s**, effectively
unchanged from 8.17/32.69. Submission counters do not certify distinct display
swaps. The four-field clamp still discards elapsed time and remains unresolved.

A 35-second actual Mednafen CD boot exits successfully and its reviewed frame
34 shows both actors and `FRAME 117` (`recovery-q15-final-cd`). The geometry,
textures, poses, combat rules and camera are unchanged. The library host suite
and all five package CTests pass. The final 24-scenario live suite passes,
including KO/replay/victory, match reset and both neutral timeouts
(`recovery-q15-qualified-combat-suite`). A stop at field 155 caught an odd
snapshot publication epoch; its raw artifacts are retained and the bounded
coherent-capture helper passed the same reaction assertion at field 156. Only
torn publications may retry, with at most two additional fields; native panic,
guest errors and all gameplay assertions remain immediate failures. Host CTest
passes 42 tests; the Saturn Python suites pass 21 and saved-run reports pass 4.

Further work must reduce the larger projection/visibility/emission costs and
the approximately 12 ms authoritative tick. This increment does not justify a
claim that the current generic renderer can reach the target through scheduling
alone.
