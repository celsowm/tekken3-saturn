# Original fight HUD sprite slice

The optional `T3_ORIGINAL_HUD` build extracts 22 private sprites from the USA
`SLUS_004.02` and the two fighters' costume texture archives: two health-bar
caps, three bar fills, two round-mark palettes, twelve timer digits, the name
plate backing and both fighters' name sprites. `scripts/extract_psx_hud.py`
writes a versioned `HUD.BIN` (schema 2, `T3HUD02`) with dimensions, LUT4
texels and RGB555 palettes. The Saturn loads it from CD into VDP1 at startup.
The original source coordinates and palettes are documented in pinned
Tekken3Remake `docs/research/code/hud.md` and `tools/remake_import/hud.py`;
the Saturn code is an attributed C adaptation. Retail pixels and the
generated file remain outside git.
The tested USA executable SHA-256 is
`fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c`;
its packed schema-2 `HUD.BIN` SHA-256 is
`e905345b0cfcb6897db02bd6c2cec6276ca8012337fd73c89c7f2f2266cfc8fe`.

## Reproduce

Configure the Paul vs Law release build as described in [RELEASE.md](RELEASE.md),
adding `-DT3_ORIGINAL_HUD=ON`,
`-DT3_LOCAL_HUD_EXE="$PWD/assets/imported/tekken3-usa/SLUS_004.02"`, and
`-DT3_LOCAL_POSE_CACHE_ENTRIES=128`. Build the `tekken3_saturn` target and
disc normally. `T3_ORIGINAL_HUD_BARS=OFF` is a diagnostic option that leaves
the existing solid bars in place while retaining original timer and marks.

## Evidence, 2026-10-24

The tested candidate ELF SHA-256 is
`dae655438d7505d3bddb855239e8144a76b1ef2630e6ca94bf884c7ef45fbfc4`.
It boots as a CD in Mednafen with the original sprites visible. Saved
screenshots and `run.json` are in
`assets/imported/hud-original-cd-sustain`; the recording video was deleted.
The displayed `FRAME` submission counter is 153 at 35 seconds, 607 at 50
seconds and 1080 at 65 seconds. The active round therefore submitted 927
images in 30 seconds (30.9/s). This is a submission count, not a measurement
of distinct displayed images or real hardware.

A cache-on Ymir injection of the same candidate, sampled at 1200 and 3000
emulator fields, advanced 1801 vblanks, 1801 scheduled/committed logic ticks,
1197 submitted images and zero discarded fields. That is 60.0 logic ticks/s
and 39.88 submitted images/s in that emulator interval. The matching HUD-off
control, also with a 128-entry pose cache, advanced 1198 images and 1801
ticks. The saved endpoints are `assets/imported/hud-bars-on-probe-{1200,3000}`
and `assets/imported/hud-control-128-probe{,-3000}`. Recalculate with
`scripts/report_saturn_runs.py --cadence START END`. Startup-relative totals
include different CD loading time and are not a gameplay comparison.

The same candidate passed both host CTest builds (55/55 and 54/54), and
`scripts/saturn_combat_suite.py --with-ko --with-timeout` passed all 24 live
cases. Its private verdict is `assets/imported/hud-original-combat/suite.json`.

## 60 Hz bar state

`src/game/psx_hud_bar.c` ports the original four-field state transition:
health-to-152-pixel target, immediate damage drop, 3 pixels of refill per
logic tick, and recent damage closing by 1/16 of the gap rounded up. The
state advances in the native owned-match simulation tick, including ticks
between submitted images. The image snapshot carries the matching lengths
to VDP1; render cadence no longer changes the animation speed. Round changes
reset the bar state so each new round refills from empty.

`tests/test_psx_hud_bar_trace.py` checks the C transition against the original
after-frame HUD RAM in three pinned Tekken3Remake fight traces. It matched
33,575 transitions across Paul–Law, Lei–King and the long match, including 133
health changes, 816 refill steps and 1776 recent-damage closing steps. This
checks the bar step with the original fighter health and maximum; Saturn gameplay
still needs a whole-frame HUD parity comparison.

The `hud-bar-tick` candidate ELF SHA-256 is
`e071ed39b1922f4a7909246b104888cc8df8cd8e946acca2ce5e516a4d161eab`.
Its cache-on Ymir 1200→3000 field interval advanced 1800 logic ticks in
1799 vblanks, submitted 1195 images and discarded zero fields (60.03 ticks/s,
39.86 images/s). The original HUD control submitted 1197 images in 1801
vblanks. In an actual Mednafen CD run, saved `frame-35.png` and
`frame-65.png` show FRAME 153→1081: 928 submissions in 30 seconds (30.93/s).
The private screenshots and metadata are in `assets/imported/hud-bar-tick-cd`;
`run.mov` was deleted.

At the bar-tick checkpoint the HUD still scaled into the port's approximate
320 × 224 layout. Original PS1 layout is 384 × 480 interlaced with
152-pixel fills and 24-pixel bar height; precise screen placement, player
names, original text fonts and round/result messages were still open.

## Name plates and exact placement (2026-10-25)

`extract_psx_hud.py` now also writes the plate backing (2 × 10 at UV
(0xDC, 0xA8) through CLUT 0x7F11) and both fighters' name sprites: page 2
(word 448) of each costume texture ARC at U 0x40, sixteen rows, through the
system CLUT 0x7F10. The widths are the character records' byte +5 read
through the pinned remake's canonical-rebased USA disc (COSTUME_KEYS
0x80095CE8 key 0 for Paul and key 4 for Law, into CHAR_RECORDS 0x80098120):
Paul 32, Law 26, costume-0 plates for both. The extracted texel indices and
RGB555 palettes are byte-identical to the remake's verified `hud.py`
converter output for both names. P1's sheet pads right and P2's pads left,
so each name stays flush with its anchor when the 32-texel sheet is drawn
scaled; CMake passes the costume ARCs through
`T3_LOCAL_HUD_ACTOR0_ARC`/`T3_LOCAL_HUD_ACTOR1_ARC`.

The draw code now follows the original positions exactly. The original
384 × 480 fight screen shows 368 × 448 from (8, 20), so the Saturn's
320 × 224 maps x' = (x − 8) · 20/23 and y' = (y − 20)/2: the bars sit at
x 2 and 169, y 10 (five-pixel caps, the original 152 fill units mapping to
132), the timer digits at 139/152, y 1 (14 × 23), the round marks from
124/169 at y 25 (13 × 9, twelve apart), and FUN_8004EB9C's plates and
names at y 27 (five rows) and y 23 (28 × 8), P1 anchored at x 5 and P2's
right edge at 300. The port-only "P1"/"CPU" labels and "ROUND n" text no
longer draw under `T3_ORIGINAL_HUD`; the name plates carry both sides. The
FRAME submission counter stays.

Evidence: candidate ELF SHA-256
`ad1f402aac99288c949193851f5b585c58ccd3a9ec0e98b45b7e8eb54887e0d7`
(app 571,348 bytes). A 900-field Ymir probe
(`assets/imported/hud-names-probe`) is clean: first_error 0, no rejected
faces, draw timeouts or discarded fields, 746/2048 commands. Programmatic
pixel checks against the extracted palettes find every piece at its mapped
position in the native screenshot: both names (184 and 167 palette pixels),
both plates, the moved timer, marks and bars. A 40-second Mednafen CD boot
(`assets/imported/hud-names-cd`, `assets/imported/hud-names-cd-state`)
exits 0; the same palette check passes on its screenshots, and the save
state's diagnostics (v23) read back 1.00 ticks per vblank with 285
submitted images over the 522-vblank gameplay window (about 32/s, matching
the previous candidate's 30.93/s window). The recording videos were deleted.

The full release gate was then re-run on this commit for both release
configs and both summaries pass: `flush-trusted-hudnames` (the HUD-off
control `build/flush-trusted`, rebuilt with the new fallback bar geometry,
ELF `40e12eaafc2a2bf5966fed0f1f86cc5a11fbf736c8ad944becf6a3d4b522bf74`) and
`hudbar-hudnames` (this candidate). Each gate ran 56 + 55 host CTest, all
26 combat cases (throw, throw-damage, ko-replay, ko-victory, next-round,
match-end and the timeout checkpoints, with measured input offsets), and
3,000/3,000 reference ticks against `build/flush-trusted-reference` (ELF
`2974df5c590d010043c60a53596ed29c27628e3d910ac102fc68c20ede661d1f`) with no
first difference and identical trace digests. The verdicts are
`assets/imported/regression-flush-trusted` and
`assets/imported/regression-hudbar-throw`.

Player names, plates and the exact original placement are therefore done.
The original text font, the round messages ("ROUND n", "READY?", "K.O.",
result texts) and the mode lines remain pending.
