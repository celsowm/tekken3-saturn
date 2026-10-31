# Building the Paul-against-Law disc

## Current demo checkpoint (2026-10-30)

The current presentation candidate is `build/demo-title`, ELF SHA-256
`0654fa4e204bc5fcd2e25f0ad447dc9e8d8b28b5cfb63e0aa7fd698447ac4106`.
Its loading screen displays `TEKKEN 3 V0.1` and
`MADE USING celsowm/LibSaturn`; its title screen displays the version, matchup
and controller mode, then waits for START. Mednafen booted its mixed-mode
disc and showed the loading credit at second 13 and title at second 19
(`demo-title-evidence/credit-visible-cd/`; recording deleted). An image
initially appeared blank in the image viewer, but the original PNGs from
13–16.5 seconds have identical SHA-256 values and show the loading screen.
A repeat of the tested build recorded zero nearly uniform frames in a
180-field sample from seconds 13–16 (`demo-loading-control-cd/`); this does
not establish continuous display correctness outside that window. A separate
audio-enabled Mednafen run booted the mixed-mode disc, received an automated
START on the title and showed an active CPU fight at seconds 43 and 47
(`demo-title-full-fight/`; recording deleted). Its 0.15-second audio windows
at seconds 43 and 46 matched the source theme at 11.72 and 14.72 seconds,
with correlations 0.94 and 0.98; intervening windows matched weakly. This
establishes sampled emulator boot and theme playback, not uninterrupted audio,
complete mixing quality or physical-hardware behavior. With START injected at field 400, a
1,600-field cache-enabled Ymir run reached the CPU fight and recorded three
hits, one block, eight swings, no unmapped sound requests and no reported
error (`demo-title-evidence/credit-fight/`). The same ELF and mixed-mode CUE
passed all 26 cache-enabled live combat cases, including replay, match reset
and neutral timeout scoring (`demo-title-combat/suite.json`). The suite used
`--title-start-field 400 --with-ko --with-timeout` and measured an input
offset of 912 fields. Its sound report contains every case and no unmapped
requests (`demo-title-combat/sound-report.json`). This does not establish
audible fidelity, physical-hardware behavior or compatibility beyond the
covered inputs.
The current host build was rebuilt and passed all 92 CTest cases on 2026-10-30.
This is host-side logic coverage and does not replace the disc or hardware
checks.
In a separate actual Mednafen boot of this ELF, a five-second CPU-fight
window (seconds 43–48) contained 299 decoded fields and 123 changes in a
cropped image excluding the HUD: 24.6 observed changes per second
(`demo-title-cadence-cd/cadence.json`, recording deleted). The crop includes
background and effects, and identical frames may reflect an unchanged scene;
this is neither a VDP1-swap count nor a real-hardware measurement.
An audio-enabled Ymir injection of this same build and disc, with START at
field 400, produced 23.4 seconds of SCSP output. Short windows at capture
seconds 12, 16, 18 and 20 matched windows of Paul's extracted stage theme
with absolute correlations 0.98, 0.80, 0.88 and 0.93 respectively
(`demo-title-audio/correlation.json`). The source offsets are not consistent
across all samples, so this supports theme playback at those points but does
not establish uninterrupted playback or mix quality. A repeat using a
shared source timeline found strong matches at capture seconds 15.5, 17.5,
18.0, 19.5 and 20.0, matching source seconds 3.67, 5.67, 6.17, 7.67 and 8.17
respectively (`demo-title-audio-repeat/timeline.json`). Some intervening
windows correlate weakly; effects masking or interrupted output are both
possible. A two-controller neutral control using the same ELF and disc had
no hits, blocks or swings, and every 0.15-second sample from capture seconds
15.5 to 21.0 at half-second intervals matched the advancing source timeline
with correlation 0.89–0.99 (`demo-title-audio-neutral/timeline.json`). This
comparison is consistent with combat sounds masking the track, but it does
not prove the cause or establish continuous playback between samples. The CD
Block logs record one repeating play command for track 2 in each run. These
were injected executions, not CD boot or physical hardware. The audio
captures were deleted.
The preparation helper now enables this title; the older no-title candidate
remains the control.

`build/tekken3-v0.1-private.zip` is a local evaluation package containing the
tested mixed-mode disc and player instructions. Its SHA-256 is
`4d8f54b46fef7098f76f30da3c99114af57ce7a089f462faa13fe7837a8a680d`.
`scripts/package_private_demo.py` verifies that all 26 combat records and
the Mednafen boot record refer to the exact ELF, CUE and BIN, then verifies
the archived disc bytes. It does not independently interpret the screenshots
or grant distribution rights; the package contains retail-derived data and a
boot image whose redistribution has not been established.
The ZIP was extracted to a fresh directory and its `music.cue` booted in
Mednafen, with START sent at the title; screenshots at 42 and 45 seconds show
the CPU fight (`demo-package-boot/`). A 0.15-second audio window at second 43
matched the stage theme at correlation 0.94; other sampled windows were weak.
The recording and extracted disc copy were removed after inspection.
The ZIP was subsequently rebuilt with clearer keyboard instructions; the CUE
and BIN hashes are unchanged from that extraction test.

The cache-enabled Ymir diagnostic profile of the same ELF over ten fields
(`demo-title-performance-profile/profile-summary.json`) attributes about 28%
of sampled Master execution to fighter-face submission and 19% to projected
skinning. It is an intrusive, short emulator trace, not a hardware timing or
release-cadence measurement; it points further optimization toward those
already-specialized SDK paths rather than the boot or audio code.
An experimental build moved the second fighter's face preparation to the
Slave only when original particle ordering was inactive. With the same
1,200-to-1,800-field CPU-fight setup, Ymir reported 334 submissions over
601 VBlanks (33.34/s), versus 349 over 599 VBlanks (34.96/s) for the
control; both scheduled all fields and showed matching health, hit and block
counts at the endpoint. This is a short emulator test, not a pixel-parity or
hardware measurement. The slower experiment and its intermediate builds were
removed; the tested renderer remains unchanged.
Increasing the pose cache from eight to 32 entries in the same CPU-fight
window produced 349 submissions over 598 VBlanks (35.02/s), compared with
349 over 599 (34.96/s) for the eight-entry control. The 32-entry build left
only 932 bytes above the reserved 16 KiB Master stack. This short test shows
no meaningful cadence gain, so the larger cache was rejected and removed.

The rebuilt `build/ptcl-full` Paul–Law candidate includes the hit flipbooks,
particles and local sound data. Its ELF SHA-256 is
`6554cf448be72b79bf1d65c0fb5eb074a170873e990bf7f2eb8c30ecd1bfb34c`.
In a 1,200-field Ymir injection with both pads connected it reported 375
submitted images and 643 simulation ticks over 1,191 VBlanks, with no first
error, draw timeout or discarded field. This neutral checkpoint does not
establish sustained combat cadence, actual CD boot or physical hardware
behavior. Its observation and configuration are in the private
`assets/imported/demo-check-live/` directory. The prior full gate below
belongs to a different ELF and cannot qualify this candidate.
The same `ptcl-full` ELF subsequently passed the 26-case Ymir live combat
suite, including KO, replay and timeout cases, with cache emulation enabled
(`assets/imported/demo-check-combat/suite.json`). This result does not qualify
the later presentation ELF.
The captured image also shows the diagnostic frame counter. A separate live
`build/demo-presentation` sets `T3_HIDE_FRAME_COUNTER=ON`; its Ymir screenshot
shows the counter removed. This candidate's ELF SHA-256 is
`767850a778d9fd695a09ff277f1cf97fb279edd1fc0fbcffbd18aef0019a7184`.
Paired 600- and 1,200-field neutral endpoints after startup show 600 simulation
ticks and 350 submissions over 600 VBlanks: 60 ticks/s and 35 submissions/s at
nominal 60 Hz, with no discarded fields. The earlier total from boot includes
startup and must not be interpreted as a steady-state rate. Evidence is in
`assets/imported/demo-presentation-600/` and `demo-presentation-live/`.
Submissions do not prove display swaps; live combat, CD boot and hardware still
need qualification for this ELF.

With port 2 disconnected, the presentation candidate's 1,600-field Ymir run
produced three hits, one block and eleven swings, but recorded one unmapped
effect request (`0x105A`). The local sound extractor now includes that effect.
The rebuilt ELF is
`97670ec503eba6b1494ed97a2d79fceeb4613adb573862810e59ad8efabac5b3`;
the repeated CPU scenario reported the same event counts, zero unmapped sound
requests and no reported runtime error (`assets/imported/demo-sound-cpu/`).
This tests request mapping and sample loading, not audible fidelity. A paired
neutral 600/1,200-field measurement on this rebuilt ELF also reports 600 ticks
and 350 submissions over 600 VBlanks with no discarded fields; evidence is in
`demo-sound-600/` and `demo-sound-1200/` under `assets/imported/`.
Mednafen booted this image from its CUE and reached the fight with the CPU
attacking Paul at 34 seconds (`demo-cd-fight/`, screenshot and logs retained;
recording deleted). That screenshot exposed placeholder cross-shaped flashes
drawn over the original effects; this ELF is therefore not a presentation
candidate despite reaching the fight.

The placeholder flash is now suppressed when the ordered original particle
compositor is enabled. In configurations retaining the fallback, only hit and
block events create a flash; sound requests no longer create spurious crosses.
The rebuilt `build/ptcl-full` ELF SHA-256 is
`727ee1c9ce2788e55b0797587e8c31927356f088f18b5ba1d2f74789464f22fa`.
It reached the CPU fight from its CUE in Mednafen; the 34-second screenshot
shows the same encounter without the crosses (`demo-fx-cd/`, recording
deleted). A 1,600-field Ymir CPU run recorded three hits, one block, eleven
swings, zero unmapped sound requests and no reported runtime error
(`demo-fx-cpu/`). The 26-case suite on this ELF was stopped when the newer
audio-corrected candidate superseded it, and its incomplete output was removed.
Neutral paired endpoints on this ELF (`demo-fx-600/`, `demo-fx-1200/`)
recorded 600 ticks and 350 submissions over 599 VBlanks with no discarded
fields. This remains a neutral injection measurement, not a hardware or
combat-cadence qualification.

The sound extractor previously treated fighter sound request IDs as VAB tone
codes. It now resolves the low 12-bit ID through the USA sound table before
selecting a sample, including landing and guard sounds. System requests use
their table IDs too. `scripts/inspect_psx_sound_ids.py` reports the resolved
code, VAG, rate and sample length for effect requests or system IDs, without
writing converted assets. For example:

```sh
python3 scripts/inspect_psx_sound_ids.py \
  --exe assets/imported/tekken3-usa/SLUS_004.02 --kind effect 0x104f 0x105a
```

The rebuilt `build/demo-presentation` ELF SHA-256 is
`b7049705b5885ebc652bb77600bca4bc46d9360d430c69a1e05657248489f3fa`.
Its 1,600-field CPU probe (`demo-audio-fix-cpu/`) reported three hits, one
block, eleven swings, no unmapped requests and no reported runtime error.
Seven sound-extractor tests pass. This checks selection and sample loading for
that scenario; it does not verify audible mixing or cover every move's sounds.
The same ELF passed all 26 cache-enabled Ymir combat cases, including KO,
replay, match completion and timeout (`demo-audio-fix-combat/suite.json`).
Every case reported its sound counters and zero unmapped requests. This does
not qualify the later CD-DA ELF or moves outside the suite.
During the audio-enabled check, the user reported hearing the round-one and
fight announcements, move sounds and Law's voice. This is a listening check,
not a complete sound or mixing comparison.
The same ELF booted from CD in Mednafen and reached the CPU encounter
(`demo-audio-fix-cd/`). A separate 35-second run with the recorder's `--audio`
option produced a stereo audio stream; the last 5.8 seconds measured a mean
level of -14.0 dB and a peak of -1.0 dB (`demo-audio-check-cd/audio-levels.log`).
Its 32-second screenshot shows the round intro. A screenshot at 34 seconds was
not a usable fight image, so that run alone does not establish correct audio or
video throughout the fight. Both recordings were deleted after inspection.
A repeat with audio enabled (`demo-audio-video-check/`) shows the round intro
and fight at seconds 33, 34 and 35, including Law hitting Paul; the prior bad
image did not recur at those sampled times. This does not establish whether the
first bad image came from recording, decoding or the game, or prove continuous
video correctness. The repeat recording was also deleted.

The build does not yet play stage music. The pinned remake identifies Paul's
theme as track 7; `scripts/extract_psx_stage_music.py` extracts a requested
track from a local disc into `assets/imported/`, using its XA decoder, and
also converts it to 22,050 Hz stereo signed 8-bit PCM. The default arrange
track is stream 42, stereo at 37,800 Hz, 218.24 seconds, and loops. The
private extraction is `demo-stage-candidate/`; it is not part of the disc.
The superseded `demo-stage-music/` extraction was removed.

A prototype using the SDK's single-track asynchronous disc scheduler and
its 64 KiB stereo rings failed to link: the complete build overflowed Low
Work RAM by 131,364 bytes. The prototype was removed and the working build
restored. The larger two-track `sat_music` implementation holds roughly
288 KiB of its own buffers, so using it as-is is not a solution. Music needs a
smaller streaming design or substantial reclaimed memory, then Sound RAM,
CD bandwidth and combat-cadence checks.

An alternative opt-in `T3_STAGE_CDDA=ON` plays a mixed-mode disc's audio track 2
after the boot-time CD reads. It follows LibSaturn's `dino_demo/music_cdda.c`
and `SCSP_AUDIO_STREAMING_GUIDE.md`: the CD Block plays the explicit TOC range,
and SCSP EXTS0/1 route the stereo signal at -6 dB to leave mixing headroom.
It avoids the failed PCM-stream allocation. For the experiment, LibSaturn's
`tools/convert_cd_audio.py` converted `demo-stage-candidate/stage.wav` to CD-DA,
and `tools/build_cd_audio.py` combined it with `build/ptcl-full/disc/tekken3_saturn.iso`
as the private `music.cue`/`music.bin`. All CD data reads must finish before
playback; this does not cover future in-fight disc loading.

The CD-DA ELF `af93d6dc9e135546d625cc5400792592c95b73da10284962f27957e1756970cd`
linked within the memory budget and booted the mixed-mode CUE in Mednafen to a
CPU fight (`demo-cdda-cd/`, screenshots and logs retained, movie deleted).
Its captured audio is non-silent, but a one-second source comparison gave only
0.117 correlation; music playback is **not yet established** by that check.
The comparison tool's resampling control matched its source at 0.99996, but
that does not exclude emulator timing or mixing differences in the capture.
Use `scripts/check_cdda_capture.py` with a Python environment containing NumPy
to repeat a correlation before deleting a recording. Hardware and full combat
qualification remain open.

The next CD-DA candidate restores the EXTS return levels after sound updates,
because LibSaturn can rewrite the same register words when using slots 16/17.
ELF `e2024d7dc4f791f71345b8f05260f096e1491d17eed25e8026d73c23c6cb1758`
booted its mixed-mode CUE and reached the fight in Mednafen
(`demo-cdda-mix-cd/`, recording deleted). A 0.15-second window at 33.2 seconds
correlates 0.89 with the extracted theme at 3.61 seconds; the 33.0-second
window similarly correlates 0.89 at 3.41 seconds. This supports theme playback
at those moments. A later 34.5-second window has no comparably strong match;
sound effects or playback interruption remain possible explanations, so
continuous playback and mix quality are not established. The previous
one-second comparison is not conclusive because a longer mixed window can
include voices and effects.

A 40-second repeat (`demo-cdda-continuity/`, recording deleted) matches the
source at capture/source seconds 33.0/3.41, 36.0/6.41, 37.0/7.41 and
38.0/8.41, with correlations 0.89, 0.92, 0.89 and 0.80 respectively. Weaker
matches during 34–35 seconds coincide with the attack sequence; these sampled
windows support playback resuming at the expected track position, but do not
prove uninterrupted output or acceptable mixing throughout. The matching
mixed-mode candidate can be recreated with:

```sh
python3 scripts/build_stage_cdda_disc.py --build build/ptcl-full \
  --wav assets/imported/demo-stage-candidate/stage.wav \
  --libsaturn /path/to/libsaturn
```

The build must have `T3_STAGE_CDDA=ON` and `T3_STAGE_CDDA_WAV` set to the
private WAV; use `--force` only to replace its existing private mixed-mode
CUE/BIN. The build invokes the helper automatically after
the data disc is generated. A fresh build from the prepared local inputs
completed this path; its ELF, app, ISO and mixed-mode BIN were byte-identical
to the tested candidate (`e2024d7d…`).
The helper uses project-local CD-audio builders, FFmpeg, and LibSaturn's tracked
Mode 1 sector encoder to convert and validate the track; it does not publish
the disc. Launch `music.cue`
for a CD-DA build: its data-only `tekken3_saturn.cue` has no audio track.

Ymir's probe accepts the mixed-mode CUE through `--disc`; it fingerprints both
the CUE and its binary. A 1,200-field CPU injection of this candidate
(`demo-cdda-ymir/`) reported three hits, one block, zero unmapped sound requests
and no reported error. The 26-case live combat suite passed on the same
mixed-mode CUE (`demo-cdda-combat/suite.json`). Injection is not a CD boot or
hardware test.
In a paired 1,200/1,800-field CPU encounter on this ELF, 600 VBlanks produced
600 simulation ticks and 348 submissions (60 ticks/s and 34.8 submissions/s
at nominal 60 Hz), with no discarded fields. Across the complete 1,800-field
endpoint the event counters record four hits, one block and thirteen swings,
with zero unmapped requests or reported runtime errors (`demo-cdda-ymir/`,
`demo-cdda-cpu-1800/`). These are emulator submission and simulation rates,
not verified distinct display swaps or real-hardware timing.

The local image contains retail assets and its native boot image is marked
non-redistributable. It is not a public itch.io package. A distribution method
and any needed rights remain to be resolved alongside gameplay, presentation
and platform qualification.

The repository holds no retail data. A disc is built locally from your own USA
Tekken 3 import (`scripts/psx_disc_import.py`, then the `build_local_*` and
`extract_psx_*` scripts, each run by CMake from the private paths below).

The new preparation helpers consolidate that local workflow:

```sh
python3 scripts/prepare_paul_law_assets.py \
  --source '/path/to/Tekken 3 (USA) (Track 1).bin' \
  --music-cue '/path/to/Tekken 3 (USA).cue' \
  --out assets/imported/my-paul-law
python3 scripts/build_paul_law_demo.py \
  --assets assets/imported/my-paul-law --build build/my-paul-law \
  --prefix /path/to/saturn-prefix --libsaturn /path/to/lib-saturn --music
```

Both require existing tools and an installed SDK; the build directory must be
empty. Omit `--music-cue` and `--music` together for a data-only build. A test
import matched the current candidate's three motion banks, Law files, pose
tables, input sequences and stage WAV; a fresh music-enabled configuration
matched its boolean and numeric settings apart from the top-level `T3_PARTICLES`
switch, which enables the same individual particle settings. The complete
fresh music-enabled build succeeded and produced the same ELF, app, ISO and
mixed-mode BIN as the candidate in the original workspace. A subsequent test
from an extracted source kit exposed a hardcoded camera executable path; the
build now accepts `T3_LOCAL_CAMERA_EXE` and the helper supplies the prepared
executable. The extracted kit built successfully after this fix, but its ELF
(`89335d4f229ff9c95e54ec664208205ad085269b243e3d2387069a4d0e048e8e`)
and disc differ from the tested candidate, so their runtime has not yet been
qualified to the same extent. A 1,200-field cache-enabled Ymir injection of
that build recorded three hits, one block, five swings and zero unmapped sound
requests (`demo-source-kit-probe/`); it does not establish CD boot or hardware
behavior. Outputs remain private and this is not yet an itch.io installer.

The SDK used for these checks had local changes at
`d0f1eac57c9662966d624ba7342467c558823164`. The configured demo needs its
rotation-bitmap API and capacity for 59 resident sounds; the base revision
has neither that API nor more than 24 sound slots. The relevant local changes
are recorded as [a patch](../patches/libsaturn-paul-law.patch). Fighter packs
also need `sat_character_asset_compact`, recorded as
[a second patch](../patches/libsaturn-fighter-packs.patch). The first was checked
against the base revision's index and reverse-checked against the current SDK
working tree. To use it with an existing clean checkout at that revision:

```sh
git -C /path/to/lib-saturn apply --check /path/to/tekken3-saturn/patches/libsaturn-paul-law.patch
git -C /path/to/lib-saturn apply /path/to/tekken3-saturn/patches/libsaturn-paul-law.patch
```

Rebuild and install that SDK into its existing prefix before building the game.
`scripts/build_paul_law_demo.py` checks the installed archive for the bitmap
symbol and the sound registry size before configuring. To run this separately:

```sh
python3 scripts/check_demo_sdk.py --prefix /path/to/saturn-prefix
```

The registry-size check recognizes the ABI of the revision and patch above;
a changed or stripped registry layout is reported as unverified, not accepted
as proof of compatibility. The SH-2 `nm` executable must be on PATH, or supplied
to the standalone check with `--nm`. This checks the installed archive, which
can be older than its source checkout.
The patch has not yet been built and qualified in isolation; other local SDK
changes may also affect behavior. The music builder no longer relies on
untracked SDK CD-audio scripts: the two builders are included here, and a
rebuild of the candidate's mixed disc matched its BIN byte for byte.

By default, LibSaturn derives the native security and region payloads from
its installed YAUL template and marks those inputs as not cleared for
redistribution. If appropriately licensed payloads become available, pass a
LibSaturn-compatible manifest with `--boot-payloads /path/to/payloads.json`
to `build_paul_law_demo.py`, or set `T3_BOOT_PAYLOADS` when configuring CMake.
The explicit manifest was verified to reach the disc-build command using the
existing local manifest; that test does not establish rights for it or make
the retail-derived game assets redistributable.

To make a source-only archive of the current worktree and pinned research
submodule, run `python3 scripts/package_paul_law_source.py --out
build/paul-law-source.zip`. The archive includes a file-hash manifest and
excludes private imports, disc images, SDK builds and research work data. It
requires a separate toolchain and SDK plus the user's own compatible disc;
it is not a playable download, and its contents still carry their respective
licenses. Review the archive and applicable rights before distribution.

The 2026-10-27 gate in `assets/imported/parity-effects-pool-gate/summary.json`
passes 66/66 and 76/76 host tests, 26 live combat cases and a 3,000-tick
independent-camera/effect-pool reference.
Playable ELF: `8faa85fa307d59c4f8bee592d5182ded72ad3b481a4aacc5cc0d823722402e89`.
This is finite gameplay/reference coverage, not a completed 1:1 port;
[EFFECT_POOL_INTEGRATION.md](EFFECT_POOL_INTEGRATION.md) records its passing
varied-input native reference, standalone prefilled-RAM result and coverage
limits. Native audio, particle drawing, image and cadence qualification remain
separate. The preceding `parity-owned-camera-gate` result belongs to its
recorded pre-pool ELF `79c0324f…`.

1. Toolchain and SDK: `PATH=$HOME/dev/saturn-tools/bin:$PATH`, one LibSaturn
   prefix (`~/dev/saturn-prefix`, rebuilt in place from `~/dev/lib-saturn`).
2. Configure. `scripts/print_release_configure.py <existing build> <new build>`
   prints the full command from a working build directory; the options that
   matter are `T3_LOCAL_*` (private inputs: both fighters' meshes, banks and
   cameras, the stage, the physics, reaction and AI tables, `T3_LOCAL_SFX`),
   the renderer set (`T3_SLAVE_SIMULATION`, `T3_FUSED_ACTOR_PREPARATION`,
   `T3_VDP2_FLOOR`, `T3_PSX_CAMERA`, ...) and optionally `T3_TITLE_SCREEN=ON`.
3. `cmake --build <build>` writes `<build>/disc/tekken3_saturn.cue` (+ `.bin`).
4. Gate before sharing a disc image with anyone (it contains retail data and
   must not be published):
   - `ctest --test-dir build/host` and the oracle cases of
     `docs/FIGHT_ORACLE.md` (`build/host`, `build/host-law`);
   - `python3 scripts/saturn_probe.py --workload paul-law-release ...`
     (`first_error` 0, `loop_rate_x10` near 300);
   - `python3 scripts/saturn_combat_suite.py --workload paul-law-release
     --with-ko --with-timeout ...` (26 cases, including throw and throw damage).

   `scripts/saturn_regression.py` runs the CTest builds and the 26-case suite
   in one command and writes a single JSON verdict; see
   [SATURN_VALIDATION_SCRIPTS.md](SATURN_VALIDATION_SCRIPTS.md#run-the-release-gate).
   The standalone commands remain the reference for what each gate asserts.

Controls: D-pad move/guard, X/Y punch, A/B kick, START resets the match (the
title screen, when built in, waits for START first). With port 2 empty the CPU
plays Law and gets stronger after each match you win; with a pad in port 2 it
is a 2 player match.

Set `T3_ORIGINAL_HUD=ON` and `T3_LOCAL_HUD_EXE` to the private USA
`SLUS_004.02` to build the original fight HUD bar, mark and timer sprites into
the disc. CMake runs `scripts/extract_psx_hud.py` and packs the private
`HUD.BIN`; the ignored `assets/imported/tekken3-usa/hud/manifest.json` records
the source hash, dimensions and palettes when the script is run directly.
The name plates, normal round/result messages and REPLAY use the original
sprites/fonts (schema-4 HUD.BIN). Production matches now include the original
first-round intro; input becomes active at its frame 151. The combat suite
measures its input offset from the first controllable fight state. This option needs
`T3_LOCAL_POSE_CACHE_ENTRIES=128` in the measured release configuration to
fit High Work RAM. See [ORIGINAL_HUD.md](ORIGINAL_HUD.md) for the build and
runtime evidence.
See [FIRST_ROUND_START.md](FIRST_ROUND_START.md) for initialization, text and
HUD-phase comparisons and the remaining qualification gates.
See [REVERSAL_PROGRESS.md](REVERSAL_PROGRESS.md) for the original reversal
phase, its private tag-list input and the expanded reference schema. Current
host fixture data must embed bank bytes (`cold=False`) and use the model
parts for the original trace's costumes.

## Original hit sparks (optional)

`T3_PARTICLES` is ON by default in a configuration that already has the original
HUD, camera, VDP2 floor, owned match and private assets (and OFF otherwise);
pass `-DT3_PARTICLES=OFF` to opt out or `=ON` to force it. It forces the particle chain, the original
vertical viewport for stage and fighters, and the memory settings that link on
stock RAM (see `docs/PARTICLE_INTEGRATION_PLAN.md`). The gate for it is
`saturn_regression.py --particle-build <fixture build> --particle-real-build
<the T3_PARTICLES build>`; the fixture build adds
`T3_PARTICLE_SPRAY_SELFTEST`, `T3_PARTICLE_CELL_CHECK` and
   `T3_PARTICLE_DROP_SELFTEST`. `print_release_configure.py` preserves OFF,
zero and empty options when cloning a configuration. Particle pixels have been
compared with a PS1 capture; see `PARTICLE_INTEGRATION_PLAN.md` for the scope
and the remaining differences.
