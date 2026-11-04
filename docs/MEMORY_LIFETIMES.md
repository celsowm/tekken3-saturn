# Fight memory by phase: what is live when, and three ways to fit the ADX player

The inventory and proposals below are a historical snapshot of the production fight program
(`build/tekken3_saturn.elf`, ring-16k tree for the ADX numbers). Since that snapshot, the VDP1 candidate
has removed the Work RAM command and Gouraud staging buffers and builds into alternating VRAM banks.
The clean paired menu-driven fight builds measured 78,668 fewer bytes of WRAM-H in the candidate, with 79,840 bytes
before the reserved stack. Validation details and remaining limits are in `docs/VDP1_BANK_VALIDATION.md`. Other proposals below
are analysis, not a claim that the ADX workspace was implemented. Tool: `scripts/memory_phases.py
ELF main.c` (static: ELF disassembly plus the identifiers `main.c` uses in each phase's source lines).

## Method and its limits

- Phases: BOOT (`main` up to `enter_fight_phase`), LOADING (to the frame loop), FIGHT (the loop and every callback,
  interrupt vector and function whose address is taken), TRANSITION (`restart_program` and `sat_exec`).
- A function is FIGHT when a FIGHT root reaches it through direct calls, tail branches, pool constants followed by
  `jsr`/`jmp`, or any address taken in code or data. A function nothing calls and no phase names counts as an
  interrupt vector, so FIGHT. `main` itself holds all phases and is not counted as loading code.
- The identifiers of the loop miss calls inside header inline functions whose names the loop does not repeat; that
  can only make the "not FIGHT" set larger, so it is an **upper bound** for overlay candidates, not proof.
- For data, the tool reports which phases load a symbol's address. **That is not liveness**: a buffer whose address is
  loaded only while loading can stay live through pointers handed out then. Every data verdict below was checked in
  the source.

## Code by phase (production fight, 482,284 B of `.text`, 1,249 functions)

| Phase set | Bytes |
| --- | --- |
| LOADING and FIGHT | 331,021 |
| FIGHT only | 79,233 |
| LOADING only | 30,660 |
| LOADING, FIGHT and TRANSITION | 13,443 |
| BOOT only (without `main`) | 12,464 |
| BOOT and LOADING | 704 |
| `main` (all phases) | 7,700 |

Code that no FIGHT root reaches (BOOT or LOADING only, `main` excluded): **36,128 B in 141 functions**: game loaders
10,564 B (`t3_characters_init` 1.4 KB, `load_set`, `open_pack`, `t3_stage_vdp2_init`, `t3_sfx_init`, ...), library
loaders and initialisers 14,120 B (`sat_character_asset_decode` 3.3 KB, `sat_character_asset_compact` 1.2,
`sat_sound_create` 1.1, `sat_init` 1.0, ...), VDP/SCU/SCSP HAL set-up 11,444 B. Of those, the ADX player itself needs
at least `sat_cd_block_command` (404 B) and the SCSP upload/init functions, and the VDP2 `commit_*` family is
suspect (the fight updates layers); only the first two groups (24.7 KB) are candidates worth naming.

## WRAM-H data inventory (symbols over 4 KB; limit 0x060fc000, 17,180 B over with the player)

"Reuse" says whether the bytes can be recovered without touching live fight data.

| Symbol (object) | Bytes | Phases that use it | Reuse |
| --- | --- | --- | --- |
| `g_store[2]` (fighter_pack.c) | 93,632 | LOADING fills, FIGHT renders (hot block and compacted mesh "stay for the match") | none |
| `g_actors` (characters.c) | 79,976 | FIGHT | none |
| `g_state.command_buffer` (libsaturn) | 65,536 | FIGHT: every VDP1 command is staged here, 2,048 slots; bound for this program is FACE_CAP 1,040 + HUD 192 + report 256 + 3 = 1,491; one Ymir sample used 745 | **17,824 B** if the capacity followed the program |
| `g_projected_faces` (main.c) | 39,840 | FIGHT | none |
| `g_replay_history` | 30,004 | FIGHT (round replay) | none |
| `g_actor_geometry` | 29,600 | FIGHT | none |
| `g_material_commands` / `g_texture_descriptors` | 25,344 / 19,008 | loaded once, read by the actors every image through `a->material_commands` / `a->textures` | none |
| `g_gouraud_words` (libsaturn vdp1) | 16,384 | FIGHT: 8 B per Gouraud face, 2,048 slots, at most one per face, so 1,040 + HUD | **7,168 B** at 1,152 slots |
| `g_stage_world_scratch` | 14,328 | FIGHT | none |
| `g_file_asset_runtime` (libsaturn) | 13,100 | 32 mounts, 32 handles(16), 32 assets, 32 cache-block records and 4 prefetch records. The fight registers 1 or 2 files and no cache. In the ADX build only `sat_music_open` reads it, once, before the track starts | **about 12.4 KB** at 2 mounts, 2 handles, 2 assets, 0 cache |
| `g_branch_filter_words`, `g_ai`, `g_owned_match`, `g_effect_pool`, ... | 11,280, 10,396, 8,024, 5,840 | FIGHT | none |
| `g_texture_registry` / `g_palette_registry` (libsaturn) | 5,120 / 4,130 | BOOT, LOADING; a FIGHT user was found by address, not yet traced | up to 9,250 B, unverified |

The three that are libsaturn tables sized for any application (`command_buffer`, `g_gouraud_words`,
`g_file_asset_runtime`) hold **37.4 KB the fight program cannot use**. Nothing about them is Tekken-specific: a program
should size them, as it already sizes the music workspace. WRAM-L: 176 B over, from `g_audio_streams` (416 B).

Not reusable for the ADX workspace: no WRAM-H or WRAM-L region is both idle in the fight and large. The LOADING-only
data candidates (`g_material_commands`, `g_texture_descriptors`, `g_store`) are live through pointers. The arena tail
is per pairing: a 20,000 B workspace leaves **18 of 441** pairings short, 33,500 B leaves 26, 98,000 B leaves 148,
108,024 B leaves 172 (`report_fight_arena.py`). The worst pairs have 0 B. The workspace therefore has to be permanent.

## The three proposals, in bytes

Player today (ring 16,384): code 17,396 B WRAM-H, workspace 108,024 B (scheduler 33,160, two PCM rings 65,536, scratch
9,224, state 96). The 12,288 ring gives 91,640 B.

### A. Reuse by phase and right-size the library's tables

- Phase reuse of loading buffers: **0 B**. Nothing large is idle during the fight (above).
- Right-sizing: `command_buffer` 17,824 + `g_gouraud_words` 7,168 + `g_file_asset_runtime` 12,400 = **37.4 KB** of WRAM-H,
  plus up to 9.25 KB from the registries once traced. WRAM-L: `g_audio_streams` 416 B covers the 176 B overshoot.
- Sound RAM: 0. Complexity: moderate and in the library: each table gets its capacity from the program, by the same
  link-time provider pattern as `sat_music_codecs`, or a call before `sat_init` with caller memory. Risk: command or
  Gouraud capacity too small drops faces; the library already returns `SAT_ERR_CAPACITY` and the diagnostics count
  `faces_rejected`, and the bound above comes from the scene store, so it must be pinned by a build-time assertion and
  by the combat suite's `commands_used <= commands_capacity` and `faces_rejected == 0` over the 441 pairings.
- Effect on the 441: none by itself; it makes room in WRAM-H.

### B. Overlay of loading code

- Upper bound of code that no fight root reaches: 36,128 B, of which 24.7 KB is game and library loaders; the player
  needs a contiguous 17,396 B. The candidates are 141 functions in about 35 objects: a linker script would list them
  by section name, then the program needs a prelinked player blob, its symbol table, a disc file, and a reload after
  every `sat_exec` (the program image is read whole, so a restart restores the loaders and loses the overlay). It
  recovers WRAM-H only; WRAM-L and the 172 pairings are untouched.
- Not recommended: A recovers more WRAM-H with no new infrastructure and no proof obligation per function.

### C. Compact ADX streaming (not implemented)

Stereo 22,050 Hz ADX is 24,806 B/s compressed. SH-2 decodes one 32-frame group per channel at a time (the decode the
scheduler does now) straight into the inactive SCSP half of the stream slot (4,096 frames per half, 4,608 B
compressed): the two PCM rings (65,536 B) go. The Sound RAM stream halves exist already (32 KB of the 64 KiB stream
area); the SH-2 writes them at about 44,100 samples a second, the same decode the player does now.

| Piece | Bytes | Covers |
| --- | --- | --- |
| Compressed ring | 12,288 | 0.50 s |
| Landing buffers, 2 x 4,096 (one segment in flight, one waiting) | 8,192 | 0.33 s |
| State | ~200 | |
| **Workspace** | **~20.7 KB** | 0.83 s of CD delay on top of the 0.37 s already in the SCSP halves |

The current player rides out the menu test with 12,288 frames (0.56 s) in the WRAM rings plus the SCSP halves, so the
margin for CD stalls is the same order. Not yet measured: a fight where assets are read while the track plays.

- WRAM-H: 17,396 (code) + ~20.7 KB; WRAM-L: 0 (no arena); Sound RAM: 0 extra.
- Effect on the 441: all 441 (the arena is not used).
- Complexity: the largest. The scheduler delivers compressed bytes to a ring and a decoder fills the SCSP halves, with
  stereo lock-step, LPC history across segments and the loop reset; the stream API needs a "write into the half the
  SCSP is not playing" entry point. Risk: timing of the half swap, and CD delay margin.

### Combination

C alone needs 17.4 KB (code) + 20.7 KB = 38.1 KB of WRAM-H: it still needs A. **A (37.4 KB, plus up to 9.25 KB from
the registries) and C together fit with 8 to 17 KB to spare**; A alone fits only the player code (17.4 KB of the
37.4 KB) and leaves the 108 KB workspace homeless; B adds nothing that A does not already give. The simplest complete
solution is therefore A then C, in that order: A first removes the WRAM-H deficit and is measurable on its own (the
fight links, then the fight can be run with the current player and the arena tail on the 269 pairings that have it),
which also gives the first fight measurements of cost per frame and scheduler waits.
