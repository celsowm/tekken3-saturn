# The whole roster

All 21 original fighters are selectable and playable on the Saturn, against the
CPU or a second pad, in any pairing including mirrors. This replaces the earlier
compile-time Paul and Law build; `SECOND_CHARACTER.md` is history.

Ids follow the original: 0 Paul, 1 Law, 2 Lei, 3 King, 4 Yoshimitsu, 5 Nina,
6 Hwoarang, 7 Xiaoyu, 8 Eddy, 9 Jin, 10 Julia, 11 Kuma, 12 Bryan, 13 Heihachi,
14 Ogre, 15 Mokujin, 16 Gun Jack, 17 Gon, 18 Anna, 19 Doctor B, 20 True Ogre.
Gon, Anna, Doctor B and True Ogre borrow banks (19, 17, 18 and 14), and Mokujin
uses bank 9.

## How it works

- **One pack per fighter.** `scripts/build_fighter_packs.py` writes `Fnn.BIN`
  (hot tables, SKA1 mesh archive, logic bank, camera streams, flipbook, voices,
  PCM, name sheets) and `t3_roster.h`, whose largest counts size the fixed
  buffers. `src/saturn/fighter_pack.c` reads the two chosen packs. Every pack is
  402,360 to 582,664 bytes; the whole set is 21 files on a 14 MB data track.
- **Restart per pairing.** The title and select are their own program,
  `MENU.BIN` (`src/saturn/menu_main.c`), run before anything is loaded: `0.BIN`
  hands a cold boot to it, and it hands the picks back through `sat_exec`
  (`PHASE_FIGHT`), so each fight starts on a clean machine whatever the last left
  in VRAM, CRAM and Sound RAM. START on a finished match restarts into the select
  (`PHASE_SELECT`).
- **Title and select at 704x448.** `MENU.BIN` runs the double-density interlace
  mode; the fight stays 320x224. Each screen is one 352x448 VDP2 bitmap
  (`TITLEBG.BIN`, `SELBG.BIN`) shown at 2x width, baked by
  `scripts/pack_menu_art.py` from the original title and select overlays (logo,
  frame, face grid). Only what moves is VDP1: text, cursors, glow and the two
  126x208 portraits (`PORTnn.BIN`, 116 colours each, streamed from the disc as
  the cursor rests). Hi-res VDP1 draws codes of one 256-colour CRAM bank, so
  codes are split: text and cursors 1-7, portrait slots 8-123 and 124-239, glow
  240-247. The title follows `modes.md`: NAMCO PRESENTS, white flash, fade in
  from white, 300 frames with the blinking prompt, then the mode list. The
  frame counter is the original's 60 Hz clock; a hi-res picture takes two
  fields, so each frame advances it by 2.
- **Select rules.** Cursor rules in `src/game/select_grid.c`. Port 2 connected:
  each pad picks its fighter. No port 2: pad 1 picks both, and B takes the
  first pick back.
- **Select details.** The steel background is the original's 8 x 8 tile map
  (select.ovl `0x80118BDC`, ten 48 x 64 tiles) scrolling on its own VDP2 layer
  (`TILEBG.BIN`, NBG1); the names are the original name pictures (`NAMES.BIN`); the panel behind a portrait takes that portrait's
  dominant colour; the side with nobody yet shows the original silhouette; the
  timer is the big gold two-digit counter; sparks rise from the portraits every
  4th tick. The select music is the original XA stream 0x11, decoded by
  `scripts/extract_select_music.py` and placed first on the CD-DA track (60 s,
  4500 sectors); the fight starts the stage theme after it. All 22 portraits are
  read before the music starts, because a data read stops CD-DA.
- **Checking against the PS1.** `scripts/capture_ps1_menus.py` plays the USA disc
  in PCSX-Redux and saves the original mode list and select screens
  (`build/ps1-menus/`); the mode list ring, highlight bar, colours, silhouette,
  panel glow, plates and timer follow them. `scripts/menu_flow.py` captures ours.
- **Attract demonstration.** If nobody presses anything for the title's 300 ticks,
  `MENU.BIN` fades out and starts a CPU-vs-CPU fight (`PHASE_DEMO`, a short
  rotation of pairs); any button or the end of the match returns to the title.
  The original's opening movie is not played.
- **Not in the menus yet.** No costume choice (one costume per fighter, so a
  choice would change nothing), no menu sounds (the cursor and pick sounds come
  from the executable's own sound bank, which the Saturn build does not load),
  and Team Battle, Time Attack, Survival, Tekken Force and Practice are listed
  but do nothing: each needs fight rules the port does not have yet.
- **Fire and gas.** `src/game/psx_effect_fire.c` ports effect types 6-11, 19 and
  20; burn and projectile hit points live in `psx_match_effects.c` and
  `psx_match.c`. Kuma and True Ogre use the adjusted distance, and True Ogre has
  its sway.

## SDK changes

On LibSaturn `main` from `d9787ca`, which the game now needs:

- `sat_character_asset_compact` moves a decoded character out of its archive
  without the texture pixels, once they are uploaded. About 70% of a textured
  archive is dropped, which is what lets two stores fit in High WRAM.
- `init_ntsc_320x224` now writes SPCTL. `sat_exec` clears VDP2, and the SDK relied
  on the BIOS's value at 320 pixels, so every RGB sprite vanished in a restarted
  program that did not configure sprites itself. On the Saturn this showed as a
  select screen with text and no portraits.

## Memory

| Limit | Use | Margin |
| --- | --- | --- |
| High WRAM, below the 16 KiB Master stack reserve at `0x060FC000` | `__bss_end` `0x060FB498` | 2,920 bytes |
| Low WRAM, below the 512 bytes `sat_exec` reserves at `0x002FFE00` | `__wram_l_end` `0x002FFD00` | 256 bytes |

Both are nearly full, and the linker refuses any overflow. To stay inside, the
files in the `T3_FIGHTER_PACKS` block of `CMakeLists.txt` build with `-Os`.
Compiling `main.c` this way cost 3,346 images against 3,358 over 6,000 frames of
Paul vs Law on Ymir (-0.4%). The merged SDK's extra code later ran over the
reserve, so `psx_ground.c`, `psx_ik.c` and `stage_backdrop.c` joined them: 3,337
images over the same run.

`scripts/map_sizes.py MAP` lists what fills a RAM range.

## Verification

Commands: `docs/FIGHT_ORACLE.md` for the oracles; `scripts/roster_smoke.py` for
the Ymir pairs; `--boot-fighters P1 P2` builds a pairing that skips the menus.

- **Original-code oracles, without effects:** Paul 7 of 7 cases to the end of the
  tape, Law `oracle-law-s1` 3000/3000, CPU cases `cpu-law-s1`, `cpu-law-s2`,
  `cpu-paul-s2`, `cpu-both-s3` to the end of the tape, Ogre vs Yoshimitsu
  3000/3000.
- **Original-code oracles from round start, with effects:** Gon vs Xiaoyu
  (fireball, burn, smoke, KO by fire) 1976 ticks, Doctor B vs Paul (clouds) 2408,
  Ogre vs Gon 2138 and 2615, True Ogre vs Paul 2964 with two aim fields differing
  on one tick. These cases stop at camera `rand` when a KO replay re-creates
  sparks, as explained below.
- **Ymir, final code:** the release flow title, select, King vs Ogre with AI, KO,
  select, King vs True Ogre; the two-pad flow King vs Nina (no AI calls, P2's
  button presses produce swings). `roster_smoke.py` boots pairings for every
  fighter with both orders of some, with pose, event, combat and stack checks.
- **Host:** ctest 94/94.

Other pairs have only been exercised by the smoke run's AI, so a move-level
difference in a fighter with no oracle case would not show there.

## Known gaps

- **KO replays** do not re-create sparks or burn, because the effect log is not
  ported. The camera RNG then differs by 52 draws after each KO replay. It is
  cosmetic, but it is why the round-start oracles stop there.
- **Fire** is drawn with the glow sprites, not the 32x32 flipbooks. Gas has not
  been exercised by a real move; the AI did not use it. Gon's fireball and Doctor
  B's clouds were seen with forced spawns on Ymir and match the oracle in logic.
- **KMD attachment rows 18-24 are not rendered** (`PART_ROWS[:18]`). So True
  Ogre's sway reaches joints 5 and 6 only, its wings are missing, and its face and
  hand blend is not done. Gon's eye and mouth texture tricks are not done either.
- **Mokujin** keeps one bank, where the original picks one per round, which would
  need a disc read mid-match. It has no voices, and its stick appears only with
  the Yoshimitsu bank. The alternate-model sections 11 and 12 are unused.
- **True Ogre** uses Kuma's voice bank.
- **Content:** one stage (Paul's), costume 0 only, and a silent select screen.
- **`saturn_workload.py`** still profiles Paul vs Law only.
- **Frame rate** was measured for Paul vs Law and Nina vs Ogre, not for every
  pair; `submitted_frames` for each pair is in the smoke output.
