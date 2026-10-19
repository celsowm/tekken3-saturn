# Bounded simulation profiling

The opt-in `T3_SIM_PROFILE` build separates simulation work by committed tick.
It uses LibSaturn `f11b277bed2ddda963fa3dba0da08656baf08322` and its standalone
`saturn/profile.h` nested accumulator. Game-specific stages and the capture
adapter live here. The ordinary release build contains no profiler functions,
storage or runtime calls; no authoritative scheduling or arithmetic is changed.

## Capture and reproduction

Reuse an existing optimized private configuration:

```sh
python3 scripts/run_simulation_matrix.py \
  --baseline build/recovery-shared-int-live \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/local-simulation-matrix
```

The driver builds one diagnostic binary, preserving the baseline's compiler,
optimization, camera, models and runtime configuration. It disables the older
millisecond ring and serial geometry verifier. It runs neutral, approach,
supported attack inputs and simultaneous inputs. Supply `--ko-script` and
`--ko-fields` for a separate local script, but inspect actual phase coverage:
instrumentation changes how many simulation ticks pass between VBlank inputs.
A file named KO does not prove a KO occurred.

Capture a single workload with an already-built diagnostic binary:

```sh
python3 scripts/run_simulation_profile.py \
  --build assets/imported/local-simulation-matrix/build \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --frames 1200 --sh2-cache on \
  --out assets/imported/local-simulation-profile
```

`--device-script` selects an external controller script. The runner retains
each attempt and retries at most two extra fields only for torn diagnostic or
profiler publication. Native panic, invalid counters, unclosed spans, parser
errors and gameplay errors are not retried. To analyze a capture independently,
use `analyze_simulation_profile.py --capture PATH --out REPORT.json`.

For observed KO/replay/victory coverage, the existing adaptive live suite can
also dump `--dump-symbol g_t3_sim_profile`. Analyze its deep captures after
warmup, rather than weakening fixed combat assertions or forcing guest state.

## Timing contract and limits

The recorder uses Master SH-2 extended FRT ticks, with frequency captured in the
header. It begins at field 300, retains cumulative counters for every later
sample and a ring of the latest 128 ticks. Percentiles and phase groups cover
only that retained tail; they are not percentiles of the entire encounter.
Round/replay/activity labels describe entry state. Actor attribution applies
within placement/composition and their children; other work is labeled shared.
This does not measure Slave execution or its idle percentage.

The tick root contains the owned match step, diagnostic/presentation bridge and
character clock advancement. The match span includes copying, movement, roots,
poses, body/contact, hits and branch evaluation. Composition contains FK/IK,
blend, head tracking and part-chain work. Compare exclusive costs or the actual
root; adding inclusive costs would count nested work multiple times. The
analyzer requires exclusive totals to reconcile exactly to each root.

Branch rows, command queries and motion-cache hit/miss counters are events with
zero timing. A row is 12 logical source bytes, not 12 bytes of measured bus
traffic. Command-query count does not count every input-history entry visited.
Never equate the branch span with decoding cost alone.

Clock hooks, event counting, altered code/cache layout and interrupts affect
measurements. A rejected prototype timed every command query: neutral root
time was 14.947 ms/tick. Counting those queries without per-call clocks reduced
it to 12.847 ms/tick. The retained release simulation observation is about
11.562 ms/tick. These different windows are evidence of diagnostic perturbation,
not an exact overhead subtraction that can recover release stage timings.

A separate release-binary debugger capture at fields 1160–1171 reconciles both
CPU histograms with zero dropped frames, PC observations, events or snapshots
(`recovery-substage-hotspots-complete`). Master exclusive modeled shares include
branch walking 6.129%, command matching 1.632%, matrix multiply 3.830%, part
composition 3.613% and IK 2.822%. These are not inclusive FRT intervals or
recoverable frame percentages. The first 262144-entry capture overflowed the
Slave event buffer and was rejected; the complete run uses capacity 1048576.

The capture has sticky error flags, bounded nesting and counter-overflow checks.
A publication epoch protects ring/totals coherence. Static size checks and a
cross-language stage-ID test bind the big-endian wire format. Unit tests cover
wrap, corrupt totals, event/timing separation, open stacks and retry policy.
Failed/incomplete captures cannot become passing timing reports.

No physical timing is certified. The pinned Ymir DIVU model still returns
division results immediately without scheduling the hardware's latency. Use
uninstrumented endpoints for cadence and the existing independent Mednafen
checks for timing-sensitive conclusions.

## First controlled results

Evidence: `recovery-substage-matrix`, 1200 fields per basic workload, field-300
warmup. All costs below are diagnostic FRT intervals, not release costs.

| Workload | Timed ticks | Root ms/tick | Branch rows/tick | Command queries/tick | Motion cache misses |
| --- | ---: | ---: | ---: | ---: | ---: |
| Neutral | 562 | 12.847 | 280.00 | 144.18 | 0 |
| Approach | 536 | 13.188 | 278.02 | 140.14 | 104 |
| Attack inputs | 532 | 13.211 | 196.48 | 95.76 | 465 |
| Simultaneous inputs | 564 | 12.557 | 237.91 | 122.31 | 67 |

Neutral composition is 5.807 ms/tick inclusive: part composition 1.816,
IK 1.775, FK 0.631 and head tracking 0.266 ms/tick are nested observations,
not extra costs to add to composition. Branch traversal is 3.096 ms/tick
inclusive; the bridge is 1.256 and match copying 0.724 ms/tick.
No animation-cache miss occurs in neutral, so increasing that cache is not
the first idle optimization. Misses in active workloads remain visible.

The fixed KO script in this matrix ended in ordinary FIGHT; it does not
qualify replay coverage. The adaptive combat suite subsequently produced an
observed replay capture (`recovery-substage-profile-combat-suite/ko-replay`).
Its retained tail includes 114 ticks at entry state 4 / replay mode 1: root
median 9.590 ms, p95 11.551 ms, composition 6.566 ms/tick inclusive and no
branch evaluation in that subset. The bridge still costs 1.256 ms/tick.
This distinguishes active fight branch work from replay pose/presentation work.

## Qualification and next decision

The current uninstrumented authoritative core passed all three continuous
original/host/Saturn sequences: 5481, 5632 and 5221 ticks, totaling 16334 with
no divergence (`recovery-substage-host-*`, `recovery-substage-reference-*`).
Its release core archive has identical instruction/relocation disassembly to
the pre-instrumentation core. The application image is not byte-identical:
frontend panic line constants and layout change. No profiler symbols are
linked when disabled. Fresh disabled endpoints retain exactly 195 submissions
and 780 logic ticks over 1199 VBlanks: 9.758 submissions/s and 39.033 ticks/s.

All 43 host CTests pass, including the profile-accounting gate; strict SH-2 C11
syntax checking covers enabled hooks. The SDK's available host gates and all
five installed-package CTests pass. A 30-second actual Mednafen CD boot of the
diagnostic binary exits normally; reviewed frame 29 shows both actors and
FRAME 100 (`recovery-substage-profile-cd`).
The diagnostic binary also passes all 24 adaptive live combat cases, including
KO/replay/victory, second round, terminal match, reset and both timeouts
(`recovery-substage-profile-combat-suite`). These behavioral checks complement
the separate continuous state comparison; they do not replace it.

The diagnostic ring occupies 105796 bytes in Low WRAM. Its measured build has
845280 initialized image bytes against the 983040-byte limit, Low WRAM end
`0x002af0ac`, High BSS end `0x060d9de8`, and the existing 16 KiB Master stack
reserve. These are linker budgets, not measured stack-watermark claims.

The next controlled candidate is removing repeated immutable branch decoding
or traversal overhead, with a bounded memory budget and unchanged eligibility
order. Benchmark against the current walker and require release whole-path
improvement; the instrumented 3.096 ms is not a promised saving. If the saving
ceiling is insufficient, proceed to generic exact SH-2 matrix kernels, then
extract only proven independent pose work for Slave. Preserve opponent head
tracking, shared cache ownership and round blend state. Renderer work remains
independently above the 30 Hz budget; this instrumentation closes no FPS gate.
