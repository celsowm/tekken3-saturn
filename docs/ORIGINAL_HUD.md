# Original fight HUD sprite slice

The optional `T3_ORIGINAL_HUD` build extracts 19 private sprites from the USA
`SLUS_004.02`: two health-bar caps, three bar fills, two round-mark palettes
and twelve timer digits. `scripts/extract_psx_hud.py` writes a versioned
`HUD.BIN` with dimensions, LUT4 texels and RGB555 palettes. The Saturn loads it
from CD into VDP1 at startup. The original source coordinates and palettes are
documented in pinned Tekken3Remake `docs/research/code/hud.md` and
`tools/remake_import/hud.py`; the Saturn code is an attributed C adaptation.
Retail pixels and the generated file remain outside git.
The tested USA executable SHA-256 is
`fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c`;
its packed `HUD.BIN` SHA-256 is
`fa9b17b970eedeb496aa4dfd25bb41121c39a7aff5ed6f1ef8ff5f3049ea5c72`.

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

The HUD still scales into the port's 320 × 224 layout. Original PS1 layout
is 384 × 480 interlaced with 152-pixel fills and 24-pixel bar height;
precise screen placement has not been ported. Player names, original text
fonts and round/result messages are still rendered by the port or absent.
