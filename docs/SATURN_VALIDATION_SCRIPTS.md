# Reusable validation commands

Run these from the repository root. Saved-run inspection uses only Python's
standard library and does not launch an emulator. GitHub monitoring requires
an authenticated `gh` executable.

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
