# Original first round and HUD continuation

Execution checkpoint: 2026-10-25. This advances packages 0/1 of
[ONE_TO_ONE_CONTINUATION.md](ONE_TO_ONE_CONTINUATION.md). The full game port
and independent camera comparison remain open.

## Initialization and timing

The production frontend now requests original first-round placement through
`t3_psx_match_config_t.start_intro`. Existing mid-fight replay seeds retain
their separate neutral initializer. Startup selects slot 0 or 2 from physical
buttons or the original RNG, trims long entry animations, starts the intro
camera, clears input and keeps the timer at 2400 until the fight begins.
The first input-enabled tick is intro frame 151; the following fight tick
decrements the timer. Later intros retain their 91-frame boundary.

The first-round path preserves an important original ordering detail: the
newly loaded fighter is inactive during stance placement. Placement binds the
row and leaves its start pending, with frame step zero; the first MoveStartAll
starts the move at pose 1. Starting it during loading would put both fighters
one animation frame ahead. Physical face/shoulder buttons are sampled at boot,
title entry and match reset for the stance choice.

Rules come from pinned Tekken3Remake `fight-frame.md`,
`core/fight/fight_simulation.gd`, `round_flow.gd` and the existing camera ports.
The C adaptations and attribution are recorded in `NOTICE.md`.

## Original-code and SH-2 comparisons

Three private original FightMain cases start immediately after round setup:

| Case | Ticks | Coverage |
| --- | ---: | --- |
| `build/parity-intro-oracle` | 240 | Neutral startup, RNG stance choice, intro exit and normal standing recovery |
| `build/parity-intro-command-oracle` | 240 | Different entry stances, a command held across input release, later release/repress |
| `build/parity-intro-contact-oracle` | 360 | The same startup followed by movement, guard and a damaging mid; both original HUD bar records on every tick |

All **840 host ticks** match every declared combat field. Native initialization
reads configuration and private banks; it does not copy initial fighter,
round, blend, contact or input-history state from the original RAM capture.
The original screen coordinates remain explicit input to this combat oracle.

The last case also passes **360/360 ticks on SH-2 in Ymir**, with matching
field counts and digests, no first difference and no guest error. Its ELF is
`8f5338b7a3e94978eb9b367dee7eeb049108c0f3bdacdeab951e95e1bf64ae82`.
Evidence: `assets/imported/parity-intro-reference/reference-comparison.json`;
fixture: `assets/imported/parity-intro-reference-export`. The digest is
`e5bcd60296a979e48766634df15ce182a1b99affe5ff67cc6b6d504f1eb4a5b4`.
The fixture contains initialization configuration, external input and screen
positions, with no expected per-tick state linked into the Saturn image.
The additional HUD comparison is a host audit, outside the existing shared
Saturn combat digest schema; it must not be described as SH-2 HUD-state parity.

The private fixture builder now distinguishes configuration initialization
from original-RAM seeding. Its manifest binds the seed and the physical
buttons. A screen-only trace is valid without sparse pre-camera draw records;
all per-tick screen coordinates are still required. Synthetic tests cover
recipe validation, manifest identity and the zero-view-record case.

## HUD messages, assets and bar phase

`psx_hud_messages.c` selects normal Arcade/VS messages with original palette,
font and coordinates. It covers ROUND/FINAL ROUND, READY, KO/double KO,
perfect/time up, winner/loser/draw and the blinking font-1 REPLAY label.
FINAL ROUND uses `max_rounds <= played`, including original draw handling.
The replay-ending tick resumes winner text immediately. Message selection
is checked by intercepting PrintText while the original message routines
execute in the MIPS harness; replay blink boundaries have a separate contract
test. The text renderer maps the original coordinates to the Saturn viewport.
The current frontend still supplies the two supported character names;
general character metadata and other-mode text belong to the remaining roster
and mode work.

The private extractor writes schema-4 `HUD.BIN`: 59 images and nine text
palettes, 24,428 bytes. Its SHA-256 is
`ee4803d8e50580871b45dee9c745880542d2450ef3aaa828889f227c035abb63`.
All **37 glyph patterns and nine palettes** match the pinned USA converter's
canonical-rebased font descriptors and VRAM. The audit also checks every
packed record against the audited files, including padding and record order.
Only the fingerprinted USA executable is accepted.

HUD bars now live in the authoritative match and update after RoundFlow,
before this tick's HitApply, with replay playback preserving them. This also
removes the old frontend HUD cache block whose SH-2 synchronization was
disabled. The contact case checks all four original fields per bar on every
tick: at source frame 289 P2 health drops from 140 to 127 HP while the HUD
still carries the previous health; the next tick updates it. Both frames
match the original. Drawing may consume the latest committed image, but it
does not advance the bars.

## Native display and memory evidence

An initial combined build (ELF `a0f2de18…beebb30`, before the cold-code size
adjustment) boots from CD in Mednafen and shows the original intro poses,
camera, ROUND 1 and READY. Screenshots and metadata are retained in
`assets/imported/parity-intro-cd`; `run.mov` was inspected and deleted.
Ymir's matching 900-field probe is clean: zero errors, rejected faces, draw
timeouts or discarded fields, with both original actors rendered. Its
500/550-field screenshots show the round title and ready message.

The subsequent release candidate is ELF
`cc0a5b465449aa2eef0e7610bd5d5c8c0d5f96c97aa86075bc4502d212b22178`,
572,928 initialized bytes. Its complete release gate passes in
`assets/imported/parity-intro-gate`: 57 + 57 host CTests, all 26 live cases
and 3,000/3,000 reference ticks. Its matched 56-second CD run is retained in
`assets/imported/parity-intro-final-cd`, without the deleted movie. FRAME
94 at second 26 and 1049 at second 55 measure 955 submissions in 29 seconds
(32.93/s). This is a submission count, not a distinct-image or real-hardware
cadence measurement.

A later presentation-only correction removes the HUD's incorrect horizontal
crop; the 2D rectangle starts at x=0. Its ELF is
`c8f9778116b5228b563f08f92c786f3b6c6298f8ac3b0626c47a50617d67da50`.
Native Ymir and Mednafen CD checks confirm the centered result in
`assets/imported/parity-hud-center` and `parity-hud-center-cd`; see
[ORIGINAL_HUD.md](ORIGINAL_HUD.md#horizontal-alignment-correction-2026-10-25).
The 26-case gameplay gate above belongs to the pre-alignment ELF; the later
change has targeted display validation and does not change simulation code.
The alignment probe also compares 87 simulation/state fields against the
earlier 900-field probe, with no differences (`alignment.json`). The matched
Mednafen save reports one draw timeout, zero discarded fields and first_error
zero; the same timeout count appears in earlier CD captures. Its cause remains
open, separately from Ymir's zero-timeout result.

The existing 3,000-tick diagnostic initially exceeded its High WRAM limit by
480 bytes. Size-optimizing only match initialization and round placement,
and omitting original-font snapshot fields from HUD-off builds, restored its
16 KiB Master stack reserve. Per-tick combat and pose optimization levels
are unchanged. Its map ends BSS at `0x060FBFC0`, leaving 64 bytes beyond the
required reserve; further diagnostic growth needs another memory check.

SDK source revision at this checkpoint is `d0f1eac` with pre-existing local
changes; the installed `libsaturn.a` SHA-256 is
`d9a0f13cd181959b7f9d34c0e670180f48d402b68f99bb4e6e0299513e787784`.
Research revision remains `e59c4da179f39b4e915ccdc547567c8479a9f611`.
No SDK copy or alternate install prefix was created.

## Reproduction

Generate the neutral case with the research venv; add the authored tape and
360 ticks for the contact case:

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/fight_oracle_export.py \
  --out build/parity-intro-contact-oracle --seed-round-start \
  --chars 0 1 --costumes 0 0 --name paul_law --styles idle idle --ticks 360 \
  --pad-script tests/fixtures/psx_intro_contact.pads
python3 scripts/fight_oracle_eval.py build/parity-intro-contact-oracle \
  --evaluator build/host-law/t3_match_replay_eval --initialize-match \
  --export assets/imported/parity-intro-reference-export
python3 scripts/build_saturn_variant.py \
  --baseline build/flush-trusted-reference --out build/parity-intro-reference \
  --cmake-option=-DT3_LOCAL_REFERENCE_REPLAY="$PWD/assets/imported/parity-intro-reference-export"
build/asset-venv/bin/python scripts/saturn_reference_suite.py \
  --build build/parity-intro-reference --export assets/imported/parity-intro-reference-export \
  --workload diagnostic --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/parity-intro-reference --frames 8000 --timeout 180 --sh2-cache on
build/asset-venv/bin/python scripts/audit_psx_hud_assets.py \
  --disc 'roms/Tekken 3 USA/Tekken 3 (USA).cue' \
  --hud-dir build/hud-bar-tick/generated/private_hud
```

Use fresh output directories or remove only the explicitly superseded outputs.
`ctest --test-dir build/host-law --output-on-failure` includes the private
first-round comparison when those fixtures exist, and skips it when absent.
The ordinary synthetic match test also checks input suppression, the first
accepted command and failure when an entry slot is unavailable.

## Remaining work

The candidate gameplay gate, native CD boot and initial cadence window pass
as scoped above. Distinct display cadence, sustained combat intervals and
real hardware remain separate performance requirements.
Original announcer/event generation still needs the 60 Hz event track; the
current audio polling is per submitted image. The independent camera path,
round-mark blinking, remaining fonts/mode text and full audiovisual comparison
remain open. The subsequent [throw-escape checkpoint](THROW_ESCAPE.md) ports
the original tap-latch conditions. Reversals, launch/get-up and stance coverage
remain further combat work.
These are not certified by the first-round checks above.
