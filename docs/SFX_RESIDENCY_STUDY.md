# Fight effect residency and the joint Sound RAM budget (study, 2 Nov)

No gameplay code or CD-DA path was changed. The only code change is diagnostic: per-sound request counters in
`src/saturn/sfx.c` (`g_t3_sfx_shared_plays`, `g_t3_sfx_voice_plays`), read by `scripts/saturn_probe.py`.
Residency (bytes held in Sound RAM) is not polyphony (voices playing at once); this study measures residency and
request frequency only. Tools: `scripts/report_sfx_residency.py`, `scripts/measure_sfx_requests.py`,
`scripts/simulate_c352_sample_cache.py` (now with an offline packing estimate), `scripts/render_c352_reference.py`.

## 1. Reference render of the Arcade theme (front 1)

Rendered with libvgm's C352 core (a port of MAME's `c352.cpp`), commit `61b62d64`, `vgm2wav --samplerate 88200
--loops 2 --fade 0`, no resampling (the chip's native rate here). Local only, git-ignored:
`assets/imported/arcade-render/paul_arcade_c352_88200.wav` and its `.json` manifest.

| Item | Value |
|---|---|
| VGM SHA-256 | `6f0cb19f51e4f2c28d18159330115778a520fc0dd8480de6c159017f97527e09` |
| ROM check | `tet1wave0.5` / `tet1wave1.4` match MAME's SHA-1; all 21 VGM data blocks equal the ROM bytes |
| WAV SHA-256 | `c1eb287ac3b14cef0560acf35832fa310d94653262ae33ce912939f59991df76` (a second render is identical) |
| Format | 88,200 Hz, stereo, 16-bit, 206.069 s, peak 25,237, no clipping, RMS -18.4 dBFS |
| Timeline | intro 0-54.806 s; loop 54.806-130.437 s (75.631 s); the render plays the loop twice (ends 206.069 s) |

This WAV is the oracle for a later SCSP sequencer. No C352/SCSP equivalence is claimed: only the C352 side exists.

**Correction found on the way:** the older analyzer read VGM header field 0x1C (a byte offset) as the loop start
(15.98 s). The loop starts at 54.8 s. `ARCADE_C352_*` documents were fixed; peak live sizes did not change, loads and
CD traffic did.

## 2. What is resident today (Paul vs Law): 400,596 bytes, 59 sounds

| Bank | Sounds | Bytes | Rate |
|---|---|---|---|
| Combat effects (`SFX.BIN`, 0x604A... 0x105A) | 19 | 150,192 | 16,042 Hz |
| Announcer (system bank) | 6 | 38,696 | 7,956 Hz |
| Paul voices (pack `F00`) | 17 | 112,028 | 7,956 Hz |
| Law voices (pack `F01`) | 17 | 99,680 | 7,956 Hz |

Run `python3 -I scripts/report_sfx_residency.py --requests assets/imported/sfxreq/runs.json` for every sound's size,
duration, hash, silence and request count. Findings:

- **Exact duplicates (2 pairs, 15,288 bytes):** `SWING` (0x604A) = `FX_6049` (4,900), and `GUARD_HAND` (0x704D) =
  `GUARD_FOOT` (0x704E) (10,388). Both are loaded twice. Across the roster, 1,903,356 voice bytes hold 1,818,432
  unique ones (the repeats are fighters that share a voice set, e.g. Kuma and True Ogre).
- **Silence:** exact-zero tails total 838 bytes and leads 572 for the 59 sounds: nothing to gain losslessly. Tails
  within 2/127 of zero total 16,765 bytes, which is lossy and also a small gain.
- **Lower sample rate:** a half-rate proxy gives 1.4-15 dB SNR (noise-like effects and 8 kHz voices): not acceptable.
- **No streaming-free trick:** the SCSP plays only PCM8/PCM16, so the 4-bit source form cannot be kept.

## 3. Request frequency

18 scripted fights (6 styles x 3 seeds, 7,200 fields each, 36 minutes of play), P1 Paul on a script, CPU Law as P2,
in headless Ymir. 0 voice steals and 0 failed plays in all. Totals:

| Sound | Requests | | Sound | Requests |
|---|---|---|---|---|
| SWING 0x604A | 1,326 | | FX_104F | 185 |
| GUARD_HAND / FOOT | 123 / 112 | | FX_1053 / FX_1054 | 188 / 158 |
| **HIT 0x7081** | **0** | | FX_105A | 3 |
| FX_7041 / 7042 / 7043 | 42 / 18 / 3 | | FX_6049 | 45 |
| FX_7044 / 7045 / 7046 | 34 / 117 / 103 | | ANN_ROUND / FINAL | 36 / 2 |
| FX_7047 / 7048 / 7059 / 7064 | 35 / 33 / 12 / 56 | | ANN_ONE / TWO / **THREE** / FIGHT | 18 / 18 / **0** / 38 |

Voice rows by pack order (see the tool for codes and roles):
Paul `[20,3,27,18,26,0,17,21,56,58,15,8,8,8,2,0,0]`, Law `[64,89,40,34,35,26,0,14,24,48,5,2,1,1,0,13,0]`.
Never requested: `HIT`, `ANN_THREE` (needs round 3), Paul rows 5, 15, 16 (`0xD94F`, `0xD94E`, `0xD94D`), Law rows 6, 16
(`0xD8D0`, `0xD8CE`): 70,532 bytes. **Absence in 36 minutes is not proof of absence**: the scripts cover
swings, hits, blocks, KO and round flow, but not every move, throw, victory quote or result screen. The run also
recorded unmapped requests (kind:player:code `3:1:1051`, `3:1:1057`, `3:0:7056`) that have no sample and play nothing today.

## 4. Can the resident effects fall below 181,128 bytes without losing audible effects?

| Step (cumulative) | Resident bytes | Note |
|---|---|---|
| Today | 400,596 | |
| 1. Load each distinct sample once (alias the 2 duplicate pairs) | 385,308 | lossless; sfx.c key mapping, not gameplay logic |
| 2. Announcer only around round starts | 346,612 | loaded before each round (largest round set 16,884 bytes) |
| 3. Drop never-requested sounds | 281,876 | **needs proof**; some are probably phase sounds |
| 4. Also drop cat4/cat5 voices (not shout/damage) | 206,864 | **role assumption** (KO/victory/special) |

**No.** The floor reached by every step I can justify or even assume is 206,864 bytes, 25,736 above 181,128, and
steps 3-4 are guesses. Reaching the target needs the remaining voices and effects loaded on demand from the CD around
the moments they play, which changes the audio loader, not the gameplay.

Note on 181,128: it equals 512,000 minus the old 330,872-byte music figure with no margin and no fragmentation.

## 5. Joint budget: sequenced music plus effects (no-streaming pool, 512,000 bytes)

Music from the Arcade theme (silent notes dropped), `simulate_c352_sample_cache.py`:

| Plan | Instant loads | With CD lead (0.4 s + 2x, gap 2 s) |
|---|---|---|
| Peak live | 319,795 | 350,354 |
| Offline packing (plan known in advance) | 321,006 | 350,360 |
| Online first-fit | 400,210 | 433,312 |

Offline packing removes essentially all fragmentation, but it ignores that the loop seam must reuse addresses, so
treat 321,006 / 350,360 as lower bounds. Safety margin: 5% of the pool (25,600), an assumption.

| | Music | Margin | Left for effects | Fits step 4 (206,864)? |
|---|---|---|---|---|
| Optimistic (instant, offline) | 321,006 | 25,600 | 165,394 (190,994 without margin) | no, 41,470 short |
| Realistic (CD lead, offline) | 350,360 | 25,600 | 136,040 | no, 70,824 short |
| Online first-fit | 433,312 | 25,600 | 53,088 | no |

Conclusion: effects and music do not coexist in Sound RAM with today's loader, even at the reduction floor.
Candidate path: offline-planned music windows, effects reduced to the always-needed combat core (about 126 KB shared
after steps 1-2 plus the shout and damage voices) with the rest loaded from the CD between rounds.

## Not measured

Other fighter pairs' voice use (the build runs Paul vs Law), throws, victory quotes and result screens, polyphony
and voice pressure under mashing beyond 0 steals here, real CD seek times, SCSP-vs-C352 sound.

## Update: the three unmapped requests are resolved

`0x1051` is the same VAB sample as `0x104F` (already resident); `0x1057` (5,796 bytes) and `0x7056` (2,716) are new.
`extract_psx_sfx.py` now extracts them and stores identical sounds once, and `t3_sfx_init` makes one Sound RAM copy
for rows that share an offset and size. Result on a 7,200-field Ymir run (mash and rush): 0 unmapped requests,
Sound RAM used 393,820 bytes (was 400,596: +8,512 for the new sounds, -15,288 for the duplicates), 0 failed plays.
`SFX.BIN` shrank from 188,888 to 182,112 bytes. Step 1 of section 4 is therefore done; the table baseline is 393,820.

## Update: the loop seam (offline plan, steady state)

`simulate_c352_sample_cache.py` now packs one steady-state loop period on a circle (`circular_extent_bytes`): a sample
alive across the loop restart keeps one address, and samples live at the seam sit where the next period expects them.
Silent notes dropped.

| Eviction gap, loads | Peak live in the loop | Circular extent (seam-consistent) | Two-pass peak (intro + loop) |
|---|---|---|---|
| 0 s, instant | 311,220 | 315,030 | 321,006 |
| 2-5 s, CD lead 0.4 s + 2x | 311,506 | 325,206 | 350,360 |
| 20 s, CD lead | 344,800 | 353,080 | 381,548 |

The seam costs 3,810 to 13,700 bytes (about 1-4%) over the live peak, and 4-6 samples cross it. The overall peak
is not at the loop restart: it is at 54.3 s, just before the intro hands over to the loop, so the music needs
`max(first pass, loop)` = 350,360 bytes with CD lead (321,006 instant). The joint-budget table in section 5 keeps
these numbers. Not simulated: the first-pass plan and the steady-state plan sharing one address map for the
samples that outlive the intro.
