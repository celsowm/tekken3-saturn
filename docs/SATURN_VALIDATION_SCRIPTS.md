# Reusable validation commands

Run these from the repository root. Saved-run inspection uses only Python's
standard library and does not launch an emulator. GitHub monitoring requires
an authenticated `gh` executable.

## Run the release gate

`saturn_regression.py` runs the gates of [RELEASE.md](RELEASE.md) against one
built disc and writes a single verdict. It runs `ctest` for each configured
host build (`build/host`, plus `build/host-law` when that private bank is
configured), the 26-case live combat suite with throws, KO and timeout cases, the
3,000-tick reference replay, and optionally one frozen-pixel pair. Each gate
is a subprocess with its own log; the summary is rewritten after every stage,
so an interrupted run still records what it had proved.

```sh
build/asset-venv/bin/python scripts/saturn_regression.py \
  --build build/parity-reversal \
  --out assets/imported/parity-effects-pool-gate \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --reference-build build/flush-trusted-reference \
  --reference-export assets/imported/parity-effects-pool-reference-export
```

This effect-pool gate passed all stages on 2026-10-27: 66/76 host tests,
26 live combat cases and 3,000 independent varied-input reference ticks;
total duration 1,030.0 s. Playable ELF `8faa85fa…` and diagnostic ELF
`21ccb16a…` are bound to the full identities in
[EFFECT_POOL_INTEGRATION.md](EFFECT_POOL_INTEGRATION.md). The separate
prefilled-RAM reference also passes. This command needs a new empty output
directory for a repeat.
The preceding `parity-owned-camera-gate` passed all stages on 2026-10-27 for playable ELF
`79c0324fd4d85d25f9eefae00acbd38a63178bfcd6ebd5e04bfda3413eb54dbe`
and diagnostic ELF
`6c23db7ee1934274efa68b60a9b05eff87939819054ff0be461ae9a347a63911`:
26 live cases and all 3,000 independent-camera reference ticks. The recorded
host census is 64/73; subsequent standalone sound-ring/spark tests have
separate logs. Use a new empty output directory for a repeat.
The diagnostic tree now uses the new varied-input export and size optimization;
reconfigure it from the historical settings before reproducing an older gate.
See [OWNED_CAMERA_SH2_PARITY.md](OWNED_CAMERA_SH2_PARITY.md) for the contract.
The earlier `parity-reversal-gate` and feedback-based control export are
historical results in [REVERSAL_PROGRESS.md](REVERSAL_PROGRESS.md). Historical
exports have their own schema hashes; rebuild from their original fixture
instead of editing manifests.

Add `--frozen-baseline A --frozen-candidate B` to compare two captures of the
same committed tick and probe configuration (see
[Compare frozen presentation](#compare-frozen-presentation)); `--include-cached-stage-faces`
forwards the reconciling mode described there.

`<out>/summary.json` holds `passed`, the per-stage status, command, log path
and the gate's own numbers: the CTest census and build identity; combat case
names, measured input offset and the KO/timeout assertions; reference ticks,
matching prefix and trace digests; the frozen comparator's report. The process
exits nonzero if any selected gate fails. `--skip-host`, `--skip-combat` and
`--skip-reference` narrow the run; `--fail-fast` stops at the first failure
instead of collecting all of them. A non-empty `--out` is refused unless
`--force` is given, so a stale `suite.json` cannot stand in for a gate that
never ran. Pillow is needed only for the frozen comparison, hence the asset
Python environment.

Inspect a completed or still running gate with
`python3 scripts/report_saturn_runs.py <out>`. It reports completed stages,
their key results and whether the whole gate is complete. This also exits
nonzero until all selected gates pass.

The wrapper certifies host behavior, live Ymir gameplay and the selected
fixtures. It does not boot the CD, measure display cadence or replace
`t3_capture_input_tape`/Mednafen evidence. Legacy reference replay compares
match state under exported screen positions and sparse previous views.
The separate `owned-camera-v1` mode uses fresh configuration and physical
pads without that feedback; each comparison records `independent_camera`.
The wrapper uses the chosen fixture's length: selecting 500 or 600 ticks
does not satisfy the release gate's 3,000-tick requirement.

`saturn_probe.py` and `saturn_combat_suite.py` accept `--disc <path>` when the
build uses a mixed-mode CUE. The probe fingerprints the selected CUE and its
referenced binary files as well as the build's ELF, rather than treating the
data-only ISO as the tested disc. `saturn_regression.py --disc <path>` also
forwards the disc to its live combat gate and records its identity; its
separate original-reference build retains its own disc and configuration.
For short audio investigations, `saturn_probe.py --capture-audio` asks the
external Ymir harness for raw stereo signed 16-bit little-endian SCSP output
in `audio.s16le`. Its harness convention is 44,100 Hz; convert it with FFmpeg
using `-f s16le -ar 44100 -ac 2` before passing it to
`scripts/check_cdda_capture.py`. Keep the JSON results and delete captured
audio after inspection. This is an injected emulator run, not a CD boot.
Its optional `--timeline-offset` restricts each correlation search to a
shared expected source timeline, with `--tolerance` setting the allowed drift;
this helps distinguish repeated music phrases from consistent progression.
`scripts/analyze_loading_capture.py` counts nearly uniform frames in a short
selected video window; inspect the screenshots and compare image hashes when
the image viewer appears inconsistent. Delete the video after analysis.
On macOS, `record_mednafen.py --start-after 20` can send a Return/START pulse
to its isolated emulator process after approximately 20 seconds; it requires
Swift and accessibility access. The screenshot or gameplay evidence must
still show that the title accepted input. Keep captures short and delete the
recording immediately after analysis, as required by AGENTS.md.
`scripts/analyze_fight_cadence.py` counts exact image changes in a cropped
fight window of such a recording. It is a visual observation, not a direct
measure of display swaps or physical Saturn performance.
The live suite now waits for an observed FIGHT state before starting its
input schedule, including production round one's original intro. The
optional `--title-start-field 400` sends START at field 400 and releases it at
410 in every run, including the search for the first controllable fight; use
it with a title build whose loading completes before that pulse. The suite
still measures the fight start and refuses a manual input offset preceding the
release. The
configuration-initialized first-round reference has a separate manifest and
comparison described in [FIRST_ROUND_START.md](FIRST_ROUND_START.md).
The release-gate wrapper accepts and forwards the same `--title-start-field`
option to its combat gate.

## Inspect saved runs

Pass capture directories or explicit JSON files. Probe reports expose the HUD's
`frame_serial`, submitted frames, VBlanks, simulation ticks and error counters.
They inspect the native panic dump when present, so stale successful counters
cannot hide a captured panic. Combat-suite reports also recheck case health.
Mednafen reports summarize process exit status; inspect recordings to assess
visual output and encounter coverage.

```sh
python3 scripts/report_saturn_runs.py \
  assets/imported/recovery-hud-frame-probe \
  assets/imported/recovery-frt-final-combat-suite \
  assets/imported/recovery-hud-frame-cd
```

The command prints JSON and exits nonzero for failed runs, malformed inputs or
missing required fields. It does not rerun gameplay assertions or replace
`saturn_combat_suite.py`. Keep capture artifacts private under `assets/imported/`.

Use `--optimizations` to inspect counters outside the main state, for example:

```sh
python3 scripts/report_saturn_runs.py assets/imported/demo-audio-fix-cpu \
  --optimizations sfx_played sfx_unmapped sfx_unmapped_seen event_hits
```

`--sound` summarizes these counters for a probe or every case in a completed
combat suite, and explicitly lists cases whose counters are missing. A zero
count only covers the requests exercised by those runs.

## Compare gameplay cadence

Use two successful **uninstrumented** neutral runs from the same ELF, with the
second endpoint later than the first. The script checks ELF identity, counter
progress and probe health. Choose the actual nominal refresh rate using `--hz`
(60 by default; 50 for PAL). The rate is based on counter differences, not
wall-clock emulator throughput. The script cannot infer whether instrumentation
was enabled from these JSON files; select the correct builds yourself.

```sh
python3 scripts/report_saturn_runs.py --cadence \
  assets/imported/recovery-frt-final-600 \
  assets/imported/recovery-frt-final-1200 --hz 60
```

This pair yields approximately 8.17 submissions/s and 32.69 logic ticks/s.
Submission rate does not certify actual display swaps.

## Compare frozen presentation

With Pillow installed, compare two `T3_RENDER_BENCH_TICK` captures at the same
committed tick. The comparator requires matching BIOS, probe, cache policy,
refresh rate, CPU clock and divider model. It checks all stable diagnostic
fields and exact model-region RGB pixels, allowing timing counters and HUD
pixels to differ. This does not establish continuous state or timing parity.

```sh
python3 scripts/compare_saturn_presentation.py \
  assets/imported/presentation-baseline-frozen \
  assets/imported/presentation-per-frame-frozen
```

See the [bridge study](PRESENTATION_BRIDGE_PERFORMANCE.md) for the measured
controls and trace opt-in configuration.

When the pixels differ, `diff_saturn_screens.py` reports the bounding box, the
pixel count, the rows that differ (grouped into runs) and the largest channel
delta. `--image` also writes an amplified difference image. Probes compared
against existing baselines need `--sh2-cache on`.

```sh
python3 scripts/diff_saturn_screens.py \
  assets/imported/t30-z-frozen assets/imported/t30-cam-control-frozen \
  --image assets/imported/t30-cam-control-frozen/diff.png
```

## Wait for CI on a specific commit

```sh
python3 scripts/watch_ci.py
python3 scripts/watch_ci.py --revision 1311f77 --timeout 300
python3 scripts/watch_ci.py --repo celsowm/libsaturn \
  --revision 35647ed13af594f74f74a2c9d194b513b558ad31 \
  --workflow Package --workflow 'Phase 3 final gates'
```

By default the script resolves HEAD and waits for both `Host combat regression`
and `Saturn firmware and disc image`. Repeat `--workflow` to select other
required workflow names. Full 40-character commit hashes work across repositories;
branch names and abbreviated revisions are resolved using the local checkout.

The script includes queue time in its timeout, reports status changes with run
URLs, and exits nonzero for unsuccessful workflows, missing runs at timeout or
`gh` errors. The latest run for each workflow on the exact commit is used.

## Build and measure an isolated variant

Clone an existing optimized Release configuration into a fresh directory,
retaining its compiler, package and private asset options. Override project
options explicitly; commands and build logs stay beside the resulting ELF.
The builder refuses populated output directories and non-project overrides.

```sh
python3 scripts/build_saturn_variant.py \
  --baseline build/recovery-framed-live --out build/q15-live \
  --cmake-option=-DT3_PERF_SAMPLES=OFF \
  --cmake-option=-DT3_RENDER_BENCH_TICK=0

python3 scripts/run_saturn_stage_matrix.py \
  --baseline build/recovery-framed-live \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/isolated-old-camera --cases render-two full \
  --cmake-option=-DT3_CLOSE_CAMERA=OFF
```

The stage runner still forces its case-specific instrumentation/mode options
after overrides. Its diagnostic timing is separate from uninstrumented cadence.

## Torn diagnostic publications

The combat suite can stop exactly while the guest is publishing its snapshot.
Only `IncompleteSnapshotError` triggers a bounded replay at one additional
field, at most twice. The pad schedule and all gameplay assertions stay intact.
Rejected raw artifacts remain under `incomplete-N/`; the successful observation
records requested fields, actual fields and `publication_retry_fields`.
Native panic, malformed data and gameplay errors fail immediately. Standalone
`saturn_probe.py` retains exact-endpoint behavior and never retries implicitly.
