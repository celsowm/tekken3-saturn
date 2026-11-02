# Arcade music engine (experimental, menus only)

The full Paul Phoenix Arcade theme (intro 54.8 s, then the 75.6 s loop twice, 206.07 s) plays on the SCSP, from the
disc, sequenced by the 68000 alone, inside the real `MENU.BIN`. It is a test variant, off by default. It is **not**
in the fights: they do not leave room (see Budget). CD-DA is untouched and stays the fallback. All figures are from
Ymir, an emulator; the real drive and the real SCSP were not measured, and nobody has judged the sound by ear.

## What was added

| Piece | Where |
|---|---|
| Engine | `src/saturn/arcade_music.{h,c}`: reads `ARCMETA/ARCBANK/ARCLOADS/ARCPAGES.BIN` through the CD file system, reserves the sample region, loads samples on the planner's schedule, publishes score pages, counts everything in `g_t3_music` |
| Menu test | `src/saturn/menu_main.c`, only with `T3_ARCADE_MUSIC_DIR`: **L+R+START on the title** opens "ARCADE MUSIC TEST"; A/B/C play menu sounds over it; START leaves. No original music is replaced |
| Stand-alone player | `tests/arcade_music_player` (the engine with no game) |
| LibSaturn | `sat_sound_ram_reserve/release` (a reserved range counts in the stats), `sat_sound_driver_claim_slots` (resident voices never take the score's slots) |
| Converter | `scripts/c352_scsp_convert.py`: `--loop-passes` unrolls the VGM loop, `--max-slots` keeps the score off the SH-2's slots, `--bank-lead`, `meta.bin`, a better packer |
| Tools | `run_arcade_music.py` (player), `run_menu_arcade_music.py` (menu), `measure_menu_sound_ram.py`, `analyze_arcade_music_seams.py`, `sweep_c352_bank_extent.py` |

Data (`meta.bin`, `bank.bin`, `loads.bin`, `pages.bin`, 1.2 MB of converted samples and score) is private and
git-ignored under `assets/imported/c352-proto/`. No ROM, VGM or converted sample is in Git.

Build and run:

```
python3 scripts/c352_scsp_convert.py assets/imported/arcade-vgm/paul.vgz --end 206.07 --scale 0.5 \
    --tick-samples 128 --loop-passes 2 --max-slots 26 --bank-budget 378000 --bank-lead 1.0 \
    --pcm8-base 0xE30C --out assets/imported/c352-proto/menu-data
cmake -S . -B build -DT3_ARCADE_MUSIC_DIR=$PWD/assets/imported/c352-proto/menu-data
ninja -C build tekken3_menu tekken3_saturn_disc
python3 scripts/run_menu_arcade_music.py --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav --out assets/imported/c352-proto/menu-run
```

`--pcm8-base` is where the menu's own sounds end (0x3000 + 45,836); the score's sample addresses are absolute, and
the engine refuses to start (`base_mismatch`) if the reservation lands anywhere else. Clear `T3_ARCADE_MUSIC_DIR`
for the normal build: without it the menu has none of this code.

## 1. Budget of the menu, measured on Ymir

`scripts/measure_menu_sound_ram.py` (`MENU.BIN` alone) and the music run (`g_t3_menu_audio`, `g_t3_music`):

| | Bytes | |
|---|---|---|
| Pool (after the 12 KiB driver and the 64 KiB stream area) | 446,464 | `sound_ram_capacity` |
| Menu sounds, 5 resident | 45,836 | `sound_ram_used` before the music; 0 steals, 0 failed plays |
| Music region (reserved) | 360,248 | `sat_sound_ram_reserve` |
| **In use with the music** | **406,084 (90.95%)** | `sound_ram_used` = high water, read on Ymir |
| Free | 40,380 (9.05%) | the margin asked for was 5% (22,323) |
| Sound slots | 5 of 64 resident | the region is a reservation, not a sound |
| SCSP slots | 26 for the score, 6 for the SH-2 | the effect played on slot 26; at most 1 SH-2 voice at a time |

The driver, the 68000 sequencer and the score area are in the 12 KiB below the pool and were already counted. The
sequencer's pages live in Work RAM: the page buffer 4 KiB, the sample read buffer 64 KiB, the load table 7.7 KiB,
about 76 KiB of `.bss` in all; the code added about 7 KB to `MENU.BIN` (66,080 to 73,448 bytes).

It fits only because of two changes to the planner. The old first-fit packing needed 381,650 bytes for this song
(1.5 s lead), 401,600 with shorter leads: over the 378,305-byte limit. Trying four placement orders and keeping the
best brings the extent down to the **live peak, 360,248**, which no placement can beat
(`scripts/sweep_c352_bank_extent.py`). The lead is 1.0 s.

Not shown to fit: the fights (`docs/NINA_OGRE_SOUND_RAM.md`): the lightest pair is a mirror match, 238,896 bytes,
and 238,896 + 360,248 is 599,144 for a pool of 446,464.

## 2. The whole song, from the disc, alone on the 68000

Intro + loop twice = 206.07 s, a 128-sample tick, run in the menu (the player alone gives the same counters):

| | Result |
|---|---|
| Events executed / in the score | 61,153 / 61,153 |
| Late events, underrun ticks, bad sequence, refused pages | 0, 0, 0, 0 |
| Pages published / played | 121 / 121 (121 reads, none failed, longest 59 driver ticks = 171 ms) |
| Sample loads (residencies) | 118 of 118: 15 before the start, 103 during; 2,294,714 bytes |
| Late loads, failed reads, checksum errors | 0, 0, 0 (every residency's word sum is checked) |
| Smallest margin of a load | 262 ticks (0.76 s) before its first note |
| Largest burst the 68000 took in one tick | 79 events (the clean limit measured earlier is 80) |
| Drift, capture against the oracle | 1.7 ms over 206 s; local jitter 1.4 ms (SD) |
| Level against the C352 oracle | −0.02 dB RMS, peak +2.5 dB; 18 clipped samples (0 in the player run without menu sounds) |

The SH-2 played no note of the song and made no `sat_audio_update` call for it; menu sounds went on through the
normal path (3 presses, 1 voice, slot 26).

## 3. The seams

The loop in the VGM is 75.631 s; at 128 samples it is 26,057.375 ticks, so each pass quantises the same events at a
different phase (the oracle renders both passes bit-identically).

| | Intro to loop (54.8 s) | Loop to loop (130.4 s) |
|---|---|---|
| Counters at the seam: late events / underruns / late loads / bad sequence | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| Page and event count | page 27, 14,028 | page 73, 37,531 |
| Dropouts (50 ms under ¼ of the oracle) in ±1 s | 0 | 0 |
| Level step, capture / oracle | 8.0 / 8.2 dB | 16.2 / 24.3 dB |
| Envelope correlation (10 ms) ±1 s | 0.78 | 0.94 |
| Spectrogram cosine ±1 s | 0.93 | 0.86 |
| Same figures in 26 control windows elsewhere | mean 0.88 / min 0.41; cosine mean 0.93 / min 0.84 | |

Voices that cross a seam keep sounding (no dropout, steps no larger than the oracle's). The second pass of the loop
resembles the first less in the capture than the oracle's resembles itself: envelope correlation 0.77 (oracle
0.998), spectrogram cosine 0.94 (oracle 1.0). That is the tick phase plus the 1.7 ms drift; the weakest seconds are
the last ones of each pass (spectrogram cosine 0.82 to 0.88 against the oracle in the second pass).

## 4. Fidelity against the C352 oracle (libvgm)

Whole song, 128-sample tick, 26 slots: envelope correlation (10 ms) 0.88, spectrogram cosine 0.76, waveform
correlation at zero lag 0.61 (the waveform needs sample accuracy; the tick is 2.9 ms), pitch median −0.10 cent
(95th percentile 33 cents of beating, as in the 10 s window), 18 clipped samples with menu sounds over it (0 without), mean RMS −0.02 dB. It is the same
quality as the earlier 130 s run with the whole bank resident; limiting the score to 26 slots dropped no note
(24 slots drop 18).

## 5. What the SH-2 pays

`g_t3_music.service_*`, per frame, in the menu test (the menu runs at about 27 fps):

| | |
|---|---|
| Calls | 5,587 |
| Under 8 ms | 5,387 (96%) |
| 67 to 134 ms / 134 ms and over | 48 / 152 calls |
| Longest call | 417 ms (a 47 KB load) |
| Total | 32,966 ms in 206 s (16%), nearly all waiting for the CD Block |

A sample load blocks the frame: the CD file API is synchronous and its read latency is about 100 ms a call on Ymir,
so cutting loads into slices (tried 8, 20 and 64 KiB) makes it worse: 47, 37 and 33 s in total, 368, 252 and 200
long calls. The default is 64 KiB (the whole residency, at most 47,422 bytes). For a screen that animates
continuously the stalls would show as 100 to 400 ms hiccups about once a second; loads would have to be timed to
quiet moments or use a non-blocking read, which LibSaturn does not offer yet.

## 6. Limits and what was not done

- **Fights:** no room. The first number that fails is the mirror match: 599,144 bytes against 446,464.
- **Results and select screens:** `MENU.BIN` also runs the select screen and the results, which share the menu's
  5 sounds, so the budget above applies to them. Only the title opens the test; the select still plays CD-DA, and
  the engine will not start while CD-DA is playing (checked at start) because both use the CD Block. Neither
  screen was changed.
- **Other songs:** only Paul's VGM exists here, so no other track was converted or judged for the select, menu or
  results. The rule for any track is: live peak plus 45,836 at most 424,141 (95% of the pool), peak polyphony fit
  in 26 slots, and `sweep_c352_bank_extent.py` gives the first.
- **Real hardware:** drive seek times, the real SCSP and the stalls on a real frame were not measured. The driver
  clock is Ymir's.
- **Not heard:** the WAVs are in `assets/imported/c352-proto/menu-run/compare/` (private); nothing was judged by ear.
- **Looping forever:** the song plays 206 s once; a menu that wants it endless would restart it (not seamless:
  the pages and the bank restart) or unroll more passes (about 180 KB of pages and 1 KB of load records per 75 s pass).
