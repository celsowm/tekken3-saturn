# Private simulation-input capture and CD replay

These tools record inputs at the controller-to-simulation boundary. They never
write fighter position, health, move or round state. The captured words use
relative directions after physical-pad mapping. The ordinary interactive build
has both fixture options disabled. This does **not** establish PS1 input parity.

With the private asset configuration already present in `build/paul-perf`, enable
`-DT3_CAPTURE_INPUT_TAPE=ON`. The frontend records changes to the two input pairs
at an explicit simulation tick. Its 512-record buffer lives in Low WRAM, is
initialized through its count, and reports overflow instead of wrapping.

Run the controlled encounter:

```sh
python3 scripts/saturn_encounter_suite.py \
  --build build/paul-perf --bios "$BIOS" \
  --out assets/imported/paul-encounter-inputs
```

The script reruns each complete pad-input prefix from reset and retains standing
and crouching guards, a mid hit, and low attacks through KO. Subsequent approach
direction uses the previous observed side; it never injects game state. Each
private output retains the physical input schedule, build hashes, screenshot,
machine-readable state and `input-tape.json`. `--capture-inputs` on the generic
probe wrapper also supports arbitrary externally supplied pad schedules.

In a separately configured private combat build, set `T3_LOCAL_INPUT_TAPE` to
the successful case's `input-tape.json`. `build_input_tape.py` checks ordering,
uint16 bounds, tick zero and supported input bits, then emits a private C header.
The frontend feeds the exact recorded input pairs to the same combat step at
the recorded ticks. Rendering cadence may differ without changing the replay.
Start resets its cursor along with the match. The HUD labels this configuration
`RECORDED INPUT DEMO`. Do not present this build as live manual controller input.

Run the replay in Ymir and compare at the same **simulation tick**, not the same
emulator frame. Inspect moves/clocks, HP, reactions, roots, contacts and rounds;
retain unavailable fields explicitly. For a captured/replayed checkpoint:

```sh
python3 scripts/compare_saturn_replay.py \
  --record assets/imported/paul-encounter-inputs/low-15 \
  --replay assets/imported/paul-encounter-replay-aligned
```

The capture and replay options can coexist
so their emitted input streams can be compared independently. A matching final
observation proves the fields checked at that checkpoint, not every intermediate
state or PS1 parity.

Record the actual demonstration CUE separately:

```sh
python3 scripts/record_mednafen.py --build build/paul-demo --bios "$BIOS" \
  --out assets/imported/paul-encounter-mednafen --seconds 125 \
  --screenshots 20 25 30 35 40 50 60 70 80 90 95 100 105 110 115 120
```

The recorder retains ELF/app/disc hashes, command, log, return code, MOV and
ffprobe metadata. It sends SIGINT to its own process and lets Mednafen finalize
the movie. Use a new output directory for a new recording. Inspect the actual
video and relevant screenshots before claiming CD encounter coverage. Headless
Ymir injection alone never certifies boot from CD. All emitted artifacts remain
under ignored private paths; public CI uses synthetic input fixtures only.
