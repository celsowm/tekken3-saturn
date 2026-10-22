# Dense commands and simulation overlap: rejected experiment

## Decision

The next architecture in [the renderer plan](COMPILED_RENDERER_PLAN.md) was
implemented in the port and LibSaturn and exercised in both emulators. It does
not reach the 1.5x full-game continuation gate. Keep the qualified SDK `6302bec`
and frame-preparation overlap release. The 60-tick/30-image target remains unmet.

Two implementations were tested: compact the earlier native command arena,
then write dense commands directly while Master advances serial simulation.
The second establishes a usable ownership boundary, but its measured benefit
is too small to justify promoting this architecture.

## What was implemented

### Compact the native arena

Command preparation was decoupled from the experimental C32 deformation plan.
Each existing geometry worker retained its disjoint span. After painter linking,
the HAL removed unlinked reservations in place, using the command PAD word as
temporary index mapping and remapping links before moving records. This required
no second command arena and preserved cold fallback, HUD/END quota and rollback.

The former 1,773-slot endpoint fell to approximately the ordinary command count;
one moving-pose endpoint used 862 slots. These are different pose endpoints, not
a claim of matched-pose slot equality. Tests separately verify that every
submitted slot is admitted and that traversal retains its complete source order.

Both Low-WRAM and High-WRAM hot metadata were tested. Removing the compiled asset
plan allowed the 24-byte hot records to fit in High WRAM with the Master stack
reserve. Packing still adds scans and command copies to the CPU critical path.
These native pilots also retain the prototype's 48-face static-stage cache.
The direct-writer/pipeline experiment uses the ordinary production floor path;
that optional cache is not part of the qualified release.

### Write the final list directly

The replacement consumes the ordinary projected queue after both actor geometry
jobs have joined. It does not write a second native command during geometry.

Master validates the queue and stages cold clipping/Gouraud commands in WRAM.
The worker applies the existing stable painter, writes opaque commands directly
in final traversal order, and copies the staged cold pieces into that order.
If clipping expands the initial bound, Master cancels and retries the reservation
once using the measured command count. Master publishes only the used dense span
after the worker joins. No VDP/SCU access or drawing API runs on Slave.

The queue already owns the projected coordinates and cold world corners for
both poses and actor placement, together with its camera. Materials and texture
prefixes remain immutable through join. A 436-byte match snapshot and captured
round/replay facts keep the HUD consistent with those coordinates. Subsequent
serial simulation advances neither these coordinates nor their command storage.

At most two final logic ticks per iteration run on Master while Slave prepares
commands. Earlier ticks run before geometry. This preserves the existing two-CPU
actor geometry split and the original fighter, cache and input-edge order.
There is one owned visual queue, rather than a second full renderer allocation.
Timeout handling aborts the worker before reclaiming its storage.

The snapshot can trail committed simulation by two ticks, approximately one
30-Hz presentation frame. The longest measured deferred simulation block was
32 ms in the neutral and 14 basic combat runs. This is observed coverage, not
a universal worst-case latency proof or a physical Saturn measurement.

## Measurements

The qualified production recording remains 598 submissions per 60 seconds.
The experimental SDK and port changes also affect their own control, so compare
the pipeline with its newly built, matching control before comparing with that
production release. Assets, resolution, serial combat and both geometry CPUs
remain selected. All timing builds use the full original Paul pair.

Actual Mednafen CD recordings use isolated neutral pads, 90 wall seconds per
boot, and movie seconds 18 and 78 as the warmed 60-second measurement interval.
The 18-to-58 interval stays within the first live round in both variants.

| Final pair | Matching control FRAME | Pipeline FRAME | Control delta | Pipeline delta |
| --- | --- | --- | ---: | ---: |
| 1 | 59 -> 652 | 61 -> 673 | 593 | 612 |
| 2 | 59 -> 652 | 61 -> 673 | 593 | 612 |
| 3 | 59 -> 652 | 61 -> 673 | 593 | 612 |

All three alternating pairs give the same endpoints. The pipeline improves
submissions by 3.20% against its matching control, or 2.34%
against the retained 598 production result. It does not approach either the
890-submission matched continuation threshold or the plan's 897-submission
threshold when the 598 production control reproduces. Submission increments
are not verified unique displayed pictures or hardware FPS.
Every screenshot at movie seconds 18, 38, 58 and 78 is byte-identical across
the three repeats of its variant; all six runs match the pinned ELF, SDK and
asset identities. The candidate reaches 10.20 submission increments/s.
The separate same-round 40-second interval gives 394 versus 406 increments,
a 3.05% gain; the small benefit also exists before the round transition.

A separate serial-writer screening recording gives FRAME 54 -> 601, or 547
increments, 7.76% below the matching 593 control. Overlap recovers 11.88% against
that slower writer, leaving the measured net benefit at 3.20%. The serial
writer was screened once; the three-pair qualification applies to the pipeline
and control, not to every pilot variant.

The native-compaction pilots are regressions. In matched cache-on Ymir runs
over 1,800 requested fields, their Low/High variants each complete 360 submission
increments versus 376 in their SDK control. Their CD pilots give 483 and 501
increments in the same movie interval, respectively. Those CD pilots were
screening observations, without a new matched three-pair CD control; do not
mix them into the final pipeline comparison.

The direct writer alone completes 360 increments versus 374 in its initial
1,800-field control, while its pipeline completes 401 and commits 1,604 ticks
versus 1,496. All 402 recorded preparation/geometry jobs complete, with advancing
pose tick 1,602, two ticks of lag, zero fallback and no guest error or draw timeout.
Thus the worker is doing useful preparation on advancing poses. Scheduling gains
still leave discarded elapsed time and do not establish 60 committed ticks/s.

Final cache-on runs over 5,400 requested Ymir fields retain these endpoints:

| Observation | Matching control | Pipeline |
| --- | ---: | ---: |
| Observed gameplay fields | 5,374 | 5,375 |
| Committed ticks | 4,559 | 4,875 |
| Completed submission counter | 1,140 | 1,220 |
| Discarded elapsed fields | 815 | 500 |
| Late swaps | 142 | 167 |
| Draw waits / timeouts | 2 / 0 | 2 / 0 |
| Guest first error | 0 | 0 |

The pipeline completes 1,221 preparation/geometry jobs with zero fallback;
its snapshot tick advances to 4,873 with two ticks of lag. Across exactly
3,600 warmed composited-video transitions (fields 1,800-5,399), the model/stage
crop changes RGB 766 versus 819 times: 12.77 versus 13.65 changes per nominal
second. This excludes the FRAME HUD and establishes advancing visible content,
rather than counting only repeated submissions. It does not establish unique
pose epochs or 30 displayed images/s.

Matched frozen tick-120 endpoints take 48 ms for the pipeline/writer path versus
44 ms in its experimental control. These individual Ymir work samples confirm
that standalone render work still exceeds 33.33 ms; they are not distributions.

## Why the window did not deliver the required gain

A complete bounded 12-field profile retains three geometry and command tasks
and nine owned simulation calls. A preceding 36-field capture overflowed the
Slave event buffer and is excluded. The retained capture loses no events.

| CPU / completed call | Calls | Median modeled inclusive CPU cost |
| --- | ---: | ---: |
| Master owned simulation step | 9 | 257,438 |
| Master actor preparation/render | 3 | 725,860 |
| Master command preparation preflight | 3 | 64,941 |
| Master scene publication/flush | 2 | 58,919.5 |
| Slave actor geometry | 3 | 425,997 |
| Slave dense command preparation | 3 | 241,411 |

These are per-CPU interpreter costs. Nested spans overlap and the two CPU clocks
must not be aligned into a wall-time timeline. They are not hardware cycles.
The profile confirms that the new command phase executes on Slave. Master still
pays preflight, cold staging, publication, cache and dispatch costs, while serial
simulation and already-parallel geometry remain substantial work. Frozen rendering
gets slower even though part of its work can be hidden during simulation.

The final pipeline initializes 860,200 application bytes. High BSS ends at
`0x060fa7a0`, retaining the checked 16 KiB Master stack; Low WRAM ends at
`0x0027ca00`. The match snapshot is 436 bytes and the work descriptor is 40 bytes,
both in Low WRAM. No expansion RAM or second mesh/command arena is used.

## Validation and retained evidence

- All 46 port host CTests and 36 existing Saturn tool unit tests pass.
- Supported SDK host tests pass, with one explicitly unavailable test, together
  with the SDK's Python/tool checks. Focused tests cover 2,000 dirty/reused command
  arenas with holes and arbitrary painter permutations, dense publication, stable
  equal-depth order, cold-piece copying/capacity retry, residency and rollback.
- All 14 basic live combat cases pass, including hit/guard/reaction, both pads,
  simultaneous attacks, unsupported moves and reset, without command fallback.
- Matched tick-120 captures agree on 97 stable fields and exact model/stage pixels:
  RGB SHA-256 `48cdd00660bfecf7c9164266d052121b2986bc9777a9d164f18f93b3aa2fb896`.

The full 24-case round/replay suite, 16,334-tick differential campaign,
SDK consumer qualification and physical hardware acceptance are not claimed
for this rejected candidate. Those promotion checks cannot turn a failed
throughput gate into an accepted architecture.

The experimental runtime changes were archived and removed from both working
trees, preserving all earlier work. A fresh build from the restored port and
qualified SDK reproduces the preceding qualified ELF, application and raw CD
byte for byte. The restored ELF SHA-256 is
`51e7c83f96d601f275ac492832bb5bc09178208f1eec4e073459aee42e0bec69`.
The saved incremental patches were reapplied to private original-source copies
and reproduce the archived candidate files exactly.

Private evidence lives under `assets/imported/command-pipeline-source`, including
incremental/full source patches for both repositories, source/build hashes,
ELFs, maps, CMake caches, SDK archives and test logs. Release builds are
`build/prepared-command-final-{control,serial,pipeline}`; CD recordings are
`assets/imported/prepared-command-final-{control,pipeline}-cd-*`. Combat, frozen,
5,400-field video observations and the bounded profile retain their corresponding
`prepared-command-final-*` names. Earlier `dense-command-*` and
`prepared-command-*` pilots remain separate from the final release comparison.

## Consequence for the plan

Do not repeat this proposal as an untested Slave-idle opportunity or promote it
on the strength of fewer command slots. Its ownership and memory arrangement
works in the tested cases; its complete measured throughput fails the gate.

A further attempt must remove substantial simulation or geometry work before
redistributing it. The VDP2 floor remains a separate bounded possibility, with
its historical 4-5 ms cost requiring a fresh release measurement. It cannot
rescue this failed gate or close the 30-image target alone. Tick-debt retention
also remains a correctness problem, rather than a source of spare CPU capacity.

The subsequent [Paul stage integration](PAUL_STAGE_VDP2.md) measures the
current floor and implements its RBG0 replacement, together with the original
street panorama on NBG0. It preserves the outcome of this rejected pipeline;
the stage increment does not close the 30-image target.
