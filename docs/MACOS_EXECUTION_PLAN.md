# macOS build, execution and original-game validation plan

## Objective and current baseline

Build PCSX-Redux from source on this ARM64 Mac, run the verified Tekken 3
USA disc, and collect reproducible original-game observations for the Saturn
port. Completion requires an observed fight and validated capture artifacts.

- Host: macOS 27.0.1, ARM64, 24 GiB RAM; Apple command-line Clang and Homebrew available.
- Emulator: `~/dev/pcsx-redux`, baseline `2793167f`.
- Saturn library: `~/dev/lib-saturn`, baseline `fa8ed10`.
- Implementation reference: `third_party/Tekken3Remake`, pinned `e59c4da`.
- Verified original input: `roms/tekken3_usa.zip`; the USA executable passed
  the importer fingerprint check. Private compact banks and command data exist.
- PCSX-Redux was built for ARM64; OpenBIOS and the MIPS test targets were built.
- The original USA CUE runs in PCSX-Redux's interpreter. Pad-only automation
  reaches a visible Paul mirror match and records consecutive FightMain frames,
  including measured HP changes and defense. Paused Lua/RAM records agree.
  The ARM64 dynarec crash remains separate; original/Saturn parity is pending.
- Saturn BIOS and Ymir are installed privately under ignored `assets/imported/`.
- The Saturn cross-toolchain, sibling LibSaturn package and project ISO were
  built. Mednafen CD boot now visibly renders two fighters, an arena and health
  bars using `softfb` (889 recorded frames). Headless Ymir injection also runs
  the application. Scripted controls and original combat parity remain pending.

The next implementation scope and acceptance gates are in
[Next playable milestone](NEXT_PLAYABLE_MILESTONE.md).

## 1. Prepare and build the emulator

Follow the checked-out emulator's macOS CI and build scripts:

```sh
cd ~/dev/pcsx-redux
./.github/scripts/update-submodules.sh
./.github/scripts/install-brew-dependencies.sh
brew install pcsx-redux/mips/mipsel-none-elf-gcc
make -C src/mips/openbios -j2
make -j4 all pcsx-redux-tests
./pcsx-redux-tests
```

Record the compiler version, emulator revision, dependency versions, build
logs and test results. Check the executable architecture and linked libraries.
The ARM64 terminal executable and OpenBIOS build succeeded. The test suite
reported 228/238 passing initially; the four CD-ROM tests then passed after the
missing test image was generated. Six inline Lua tests still fail because the
runner treats long source strings as file paths. The interpreter run confirms
that OpenBIOS boots the USA executable; the dynarec crash remains isolated to
that execution mode. Do not treat the interpreter's intro screen as a fight.

## 2. Prepare the disc and launch an actual session

Extract the ZIP's CUE and all three tracks under gitignored `roms/`, preserving
their original names and verifying ZIP CRCs. Use the CUE to retain the audio
track layout. Verify the executable fingerprint from that extracted disc.

Launch from a private working directory under `assets/imported/tekken3-usa/`
so emulator settings, memory cards, logs and captures remain local. Use the
absolute executable, BIOS, CUE and capture-script paths. The source confirms
the startup switches `-bios`, `-iso`, `-run`, `-debugger`, `-webserver`,
`-webserver-port 8080` and `-dofile` in `src/main/main.cc`.

The CUE was verified as Tekken 3 USA (`SLUS_004.02`, label `TEKKEN3`) and
loaded with its data and CDDA tracks. OpenBIOS starts the executable. The
retained pad-automation listener now navigates the menu/quick select into a
Paul mirror match. Four thousand consecutive paired FightMain observations
include high/mid/low moves, HP changes and guarded hits, with a screenshot and
independent paused RAM/Lua agreement under `assets/imported/ps1-paul-levels/`.
The running slot is `curSlot +0xA0`, not pending `moveSlot +0x1A0` (usually −1).
The dynarec crash remains isolated to that CPU mode.
Use the interpreter as the initial debugging baseline if necessary, then
compare other CPU modes separately. Diagnose graphics, audio and CD failures
individually. The agent performs launch and inspection; request a user action
only if desktop access prevents progress after automatic alternatives fail.

## 3. Automate and capture a reproducible fight

Use the emulator's Lua and HTTP interfaces to control and inspect execution.
Confirm controller injection and frame-advance APIs in source before writing
automation; record the resulting input schedule, starting state, character,
opponent and emulator settings. Do not patch original combat state to manufacture
a capture. Keep any local savestate used for repeatability private.

Load `scripts/pcsx_redux_capture_usa.lua` through `-dofile`. Begin with idle,
movement and individual limb attacks, then guard, simultaneous hits and KO.
Capture each scenario separately, bounded by the recorder's frame limit.

Acceptance: original FightMain state 8, two valid fighter observations per
tick, consecutive guest counters, observed move/pose changes, and health changes
for a hit scenario. Retrieve a paused 2 MiB RAM snapshot using
`/api/v1/cpu/ram/raw`; decode it independently with `psx_ram_oracle.py` and
compare the same sampled state with the Lua observation.

## 4. Establish capture timing and use the observations

Run `analyze_psx_combat_oracle.py` on the real clips. Establish the USA
FightFrame, HitApply and MoveStepAll boundaries from the pinned research and
executable, and use debugger observations to check where VSync capture lands.
Check reset/load behavior and input repeatability. Record unresolved timing
offsets explicitly; consecutive VSync samples alone do not prove frame parity.

Create matched host-side scenarios from verified starting facts and inputs.
Use `compare_psx_combat_traces.py` only when both traces represent equivalent
moments. Report differences in slots, pose frames, HP and KO. Fix the portable
implementation against measured evidence and preserve regression cases without
publishing retail data. Authentic comparisons remain pending until performed.

## 5. Execute the Saturn result as a separate gate

GCC 13.2 targeting `sh2eb-elf` and binutils were built locally. The sibling
LibSaturn checkout was built and installed to `~/dev/saturn-prefix`; its host
tests passed 3/3. This project compiled `tekken3_saturn.elf` and produced a
349-sector MODE1/2352 CUE/BIN/ISO set. Boot-image checks accepted the native IP,
validated its first-read record, and confirmed that all 305,700 initialized
application bytes match the ELF load address.

The supplied North American Saturn BIOS was installed privately and its SHA-256
matches [Mednafen's documented USA/Europe BIOS hash](https://mednafen.github.io/documentation/ss.html).
The sibling LibSaturn repository tracks the complete headless Ymir harness,
including all three patches (`ymir_debug_metrics`, `ymir_misaligned_access`,
`ymir_write_watch`). They were not lost in ignored Windows files. It configures
and builds natively on macOS ARM64:

```sh
cd ~/dev/lib-saturn
cmake -S harness -B harness/build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build harness/build-macos -j4
PATH="$HOME/dev/saturn-tools/bin:$PATH" make -j4 EXAMPLE=hello_world all
harness/build-macos/probe --iso build/hello_world.iso \
  --bin build/hello_world.app.bin --bios "$LIBSATURN_BIOS" \
  --boot-frames 90 --frames 120 --out /tmp/hello-ymir.json \
  --screenshot 119:/tmp/hello-ymir.png
mednafen -sound 0 -video.driver softfb -ss.region_default na \
  -qtrecord /tmp/hello-mednafen.mov build/hello_world.cue
```

Set `LIBSATURN_BIOS` to the private BIOS path. Stop Mednafen with SIGINT to
finalize its recording. The hello-world test passed visually in both emulators:
white `HELLO WORLD!` on blue. Mednafen recorded 1,470 frames over 24.57 seconds,
confirming real disc boot and continued rendering. The software SDL driver
(`softfb`) avoids the earlier unsuccessful OpenGL capture path.

The headless Ymir probe also ran the Tekken application for 180 frames and
captured two fighters, the arena and health bars at 320×224. Its report confirms
`boot.injected=true` and the final PC inside the application. This validates
application execution, not CD boot: the harness initializes hardware through
the BIOS, then injects the binary into RAM. Responsive input remains a separate
pending check.

Repeating the Tekken CUE boot in Mednafen with `-video.driver softfb` also
succeeded: 889 recorded frames over 14.86 seconds, with two fighters, arena and
health bars visible in the final frame. This establishes real disc boot for
the current port in Mednafen. It does not yet establish responsive pads or
combat parity. Captures, reports and recordings are retained privately under
`assets/imported/saturn-smoke/`.

Require a visible fight, responsive pads and advancing simulation/renderer
telemetry. Record screenshots, logs, command budgets and measured presentation
rate. Compare matched Saturn observations with the established PS1 captures
where implemented. Emulator success and real Saturn hardware validation remain
separate outcomes. Changes needed in LibSaturn are made and tested in its own
repository, following its `AGENTS.md` and relevant subsystem instructions.

## Evidence retained locally

Build/test logs and generated ISO assets remain under the local checkouts and
ignored `assets/imported/` or `/private/tmp/` paths. Saturn scene screenshots and emulator recordings now exist in
`assets/imported/saturn-smoke/`. Original-game fight captures, bounded PS1 JSONL
captures, independent RAM checks, timing notes, trace comparisons and responsive
Saturn input telemetry remain pending.
Each stage is reported as passed, failed or pending; successful compilation
alone does not complete this plan.
