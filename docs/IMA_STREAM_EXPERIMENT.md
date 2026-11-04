# IMA stream experiment: the Paul theme from the CD, not from a sample bank

Question: can the whole Paul theme (intro and two loop passes, 206.07 s) play from the data track as a compressed
stream, so that Sound RAM holds no music bank? The idea follows a PS1 port that streams a losslessly compressed
offline render from the disc through a small ring buffer (the post the user shared). LibSaturn already has the
pieces: `sat_music` reads blocked 4-bit IMA ADPCM, and the disc scheduler (`saturn/disc_stream.h`) feeds it from the
CD while frames run.

This is a rendered track, not SCSP synthesis. The source is the libvgm C352 oracle, bounced to 44.1 kHz and
compressed. Its fidelity numbers measure the codec and the playback path. They say nothing about whether the SCSP
reproduces the C352 sequence (the open question the 68000 engine was built to test).

## What was built

| Piece | File |
| --- | --- |
| Encoder, and the decoder mirror of `src/audio/playback/ima_adpcm.hpp` | `scripts/encode_ima_stream.py` |
| Test program: CD, disc scheduler, `sat_music`, result block | `tests/ima_stream_proto/` |
| Run on Ymir, results, comparisons | `scripts/run_ima_stream_prototype.py` |
| Block lag, drift-free alignment (fractional warp), exact-decode SNR | `scripts/analyze_ima_stream_lag.py` |
| Error by block and segment phase, gain fit, band SNR | `scripts/analyze_ima_stream_error.py` |
| Pure-tone WAV for the path check (inconclusive, see below) | `scripts/make_test_tones_wav.py` |
| Regression tests for the block format | `tests/test_encode_ima_stream.py` |

Private data, git-ignored under `assets/imported/ima-proto/`: `paul/` (ima.bin, meta, decode, encode.json) and
`run/` (ymir.json, results.bin, report.json, lag.json, error.json, compare/control JSON).

## Format

Blocked IMA ADPCM as `sat_music` expects it: 1024 frames per block, a 4-byte header per channel (first sample S16BE,
step index, pad), then 1023 nibble pairs, high nibble first, channels alternating in a byte. A stereo block is
1032 bytes. `sat_music_open` requires the file size to be `ceil(frames / 1024) * 1032`, which the encoder meets.

## Results

Size and codec (software only, no Saturn in the path):

- 206.07 s, 9,087,637 frames: **9,159,000 bytes**, 25.2% of the 16-bit PCM (36,350,548 bytes), 4.03 bits per sample.
- Average disc rate: 44.4 KB/s.
- Codec SNR against the oracle: 32.4 dB. Against the oracle after the same 44.1 kHz conversion, the software decode
  reaches zero-lag correlation 0.99983, spectrogram cosine 0.9938, envelope error mean 0.0 dB (std 0.012 dB).

On Ymir (CD → disc scheduler → `sat_music` → SCSP), LibSaturn 38410b44:

- Starts at display frame 21 and runs for all 12,644 frames of the probe, past the 206 s end of the file. The same
  test on LibSaturn 771b9af6 (before the audio split) started at frame 39.
- `underruns` 0, `rejected_write_count` 0, `disc_waits` 0, 1,148 disc segments.
- Lowest ring fill 1,024 frames (about 23 ms). It never ran dry, but the margin is thin.
- `sound_ram_used` reads 0: the stream rings are outside the sample pool. LibSaturn's documentation gives 2 × 8 KB per
  stream (32 KB for the stereo pair); this was not measured in Sound RAM.
- `sizeof(sat_disc_stream_t)` 33,160 bytes. Music ring memory is not measured separately.

Capture against the oracle:

- **Drift:** the lag against the oracle is a straight line, 22.7 ms at the start, −2.3 ms at the end. Slope
  −0.122 ms/s (about 122 ppm), residual 0.01 ms. A constant clock ratio, not steps. (`analyze_ima_stream_lag.py`
  with a ±60 ms window; compare's ±8 ms window could not follow it and read it as a correlation loss.)
- After removing the drift with a fractional warp: level −0.07 dB, zero-lag correlation **0.979**, spectrogram
  cosine **0.808**, envelope error std 2.87 dB. On LibSaturn 771b9af6 the same measurement gave 0.9904, 0.913 and
  0.32 dB; the drift itself is unchanged (see below).

Against the exact decode (the stream should reproduce it sample for sample):

- Overall SNR **10.8 dB** (per second: min 0.5 dB, median 10.5 dB; 201 of 206 seconds below 20 dB). On LibSaturn
  771b9af6 it was 11.5 dB (min 1.9, median 11.3).
- By band: 0–1 kHz 28.3 dB, 1–4 kHz 4.3 dB, 4–8 kHz 0.0 dB, 8–16 kHz −0.2 dB, 16–22 kHz 0.3 dB.
- A single gain fits the capture best at 0.951 (−0.44 dB), and the SNR after that gain is 11.0 dB.
- The error is flat across the position inside a 1024-frame block (10.7–10.9 dB) and across a disc segment
  (10.7–11.0 dB). It is not a block-start or segment-boundary effect.
- The warp has its own floor. `--warp-check` puts the measured drift on the exact decode and takes it off again:
  **23.9 dB** over the full track (22.4 dB before the audio split). The stream-only figure is roughly 11 dB.

**LibSaturn audio split (771b9af6 → 38410b44).** The music path moved (`src/audio/music/`) and
`sat_music_use_disc_stream` was removed in favour of `sat_music_open` with `disc` in `sat_music_desc_t`. The
prototype was adapted to that call. The drift (−0.122 ms/s, intercept 22.67 ms) is the same, the stream runs
with no underruns or rejected writes, and the first played frame moved from 39 to 21.

Bisect: the old prototype (`dedada3`) built against 771b9af6, the current one against 38410b44, one SDK prefix
rebuilt in place, one Ymir run each, captures kept. The decoder did not change (`ima.cpp` only moved), the
ring is 16,384 frames in both, both write through `sat_audio_stream_write` (all or nothing, no partial loss),
and the streaming and HAL code is identical between the two commits. The drift is the same.

The figures above compare two start phases of the same signal. The mono sums that the metrics use hide a
stereo skew: the right channel is later than the left by a whole number of samples, measured in the raw
capture without the oracle (correlation of L with R shifted by k):

- 771b9af6: right **2** samples late (corr 0.922)
- 38410b44: right **3** samples late (corr 0.922)

Against the exact decode, per channel at the 38410b44 alignment: left SNR 23.4 dB, right **7.9 dB**. So the
right channel carried most of the loss in the overall figure.

**Cause and fix (LibSaturn 58e5d149).** `scsp::configure_slot` ends with a key-off, which writes KYONEX, and a
KYONEX applies the KYONB of every slot. The stream start arms the left slot (KYONB written), then configures the right
slot, whose key-off KYONEX keys the left slot on at that moment. The right slot is keyed by the service's final
KYONEX, after its own configuration and a `wait_one_sample`. The gap is the 2 to 3 samples, and it changes with the
start. The intent in `step_seamless_start` ("keyed on by the single KYONEX") was defeated by the HAL. `arm_key_on` now
only records the slot, and `execute_key_transitions` writes every armed KYONB right before the one KYONEX.
Measured on Ymir with the fix (`scripts/measure_stereo_skew.py`): right channel **0** samples late (was 3), overall
SNR against the exact decode 12.2 dB (was 10.8). The remaining distance to the warp floor (23.9 dB) is not explained.

**Not determined.** The loss above 1 kHz is real in this measurement, and its cause is open. Candidates: the
SCSP stream path (sub-sample timing, or a gain or filter in playback), or Ymir's audio capture path (the 122 ppm
ratio and the 0.957 gain look like an output-path property). The pure-tone track (`make_test_tones_wav.py`) was
meant to separate them but is inconclusive: its tones are periodic, so the ±60 ms lag search locks onto
neighbouring periods (fitted slope +1.85 ms/s, which this clock cannot produce, and a warp check of 5.4 dB). It
needs a non-periodic probe, such as a sweep, and a search window adapted to it.

## Sound RAM budget with the stream

`scripts/report_fight_sound_budget.py` (pool 446,464 bytes, the current profile):

| Fight pair | Resident bytes | Free in the pool |
| --- | --- | --- |
| JIN vs JIN | 238,896 | 207,568 |
| NINA vs OGRE | 436,156 | 10,308 |

The music bank in the earlier path was 360,248 bytes (the whole theme as samples). With the stream it is 0 bytes of
the pool. Both fight pairs fit with the stream's 32 KB inside the 64 KiB stream area the profile already reserves.
The earlier result, that fights did not fit, is therefore resolved by moving the music off the sample pool.

## Open points before any fight integration

- **Slots.** The stream takes SCSP slots 28-31 (`kAudioStreamScspSlotBase`). Resident sound voices are limited to
  `kVoiceCapacity - kAudioStreamCapacity` = 28 (slots 0-27), so the two do not overlap in LibSaturn; the earlier
  "26-31" worry was wrong. Checked in the source, and on Ymir in the menu (3 sounds over the stream, no steal).
- **Loop.** The track wraps at 206.07 s. The scheduler's `music_loops` counter stayed 0 across the run, so the
  wrap is not confirmed by the counter. The capture was not inspected at the wrap.
- **Time.** `ticks_per_second` read as 0 (the frequency was requested before the timer was set up), so the
  scheduler's tick counts (`max_ticks_poll` 1,103) are not converted to time.
- **Hardware.** Ymir's CD model is not a real drive. The 44 KB/s average is well under single speed, but seek and
  latency were not measured.
- **Listening.** Nothing was heard. The numbers above are the only evidence.

## ADX in the Tekken 3 menu

The same track as CRI ADX (`scripts/encode_adx_stream.py`: 10,223,608 bytes, 4.5 bits per sample, codec SNR 35.8 dB
against 32.4 dB for IMA) plays in the real menu program: `T3_ADX_MUSIC_DIR` adds `ADXMUSIC.BIN` to the disc and the
test opens from the title with L+R+A held and START (`src/saturn/adx_music.c`, run by `scripts/run_menu_adx_music.py`).

Ymir, whole track plus the wrap (10,133,504 frames consumed): `underruns` 0, `rejected_write_count` 0, minimum ring
fill 2,048 frames, 0 disc waits, 2,787 segments, three menu sounds played over the stream (menu `sfx_plays` 3, no voice
steal, no failed play), right channel 0 samples late, drift -0.122 ms/s as before, SNR against the exact decode 11.1 dB
overall and 12.1 dB median (the sounds lower the seconds they play in; the stream alone is 12.2 dB).

Memory. The menu did not link with the test: Work RAM Low overflowed by 95,496 bytes, because any `sat_asset_*` call
linked LibSaturn's built-in asset cache (4 x 64 KiB in `.wram_l`). LibSaturn c81a68ff removes it; the cache is memory the
application gives or nothing.

| Menu with the ADX test | Before | After |
| --- | --- | --- |
| `asset_cache.cpp` `.wram_l` | 262,144 B | 0 B (`g_asset_cache_blocks` is not in the map) |
| `.wram_l` total | 1,143,560 B (does not link; limit 1,048,064) | 882,952 B (165,112 B free) |
| High WRAM, test arena | 0 | 0 (the 116 KB workspace and scheduler borrow the menu's `g_io` buffer) |

Still open for the fight itself: the fight program has not been measured, and the scheduler would own the CD Block
while the fight runs.

## ADX in the fight: not integrated, the fight program has no room

(Historical. The fight now links and plays the track from a resident arena: see "ADX plays in the fight" at the end.)

Measured on the built `tekken3_saturn.elf` (the fight program; `g_io` is the menu's buffer and does not exist here).

| Region | Used | Limit | Free |
| --- | --- | --- | --- |
| Low WRAM `.wram_l` | 0x200000 + 0xffd10 | 0x2ffe00 | 240 B |
| High WRAM (`.bss` end 0x060fbf34, stack reserve 0x4000, top 0x06100000) | | | 204 B above the reserve |

Linking the player into the fight (`T3_FIGHT_ADX_MUSIC=ON`, ring 16,384) fails:

| Region | Overflow | Cause |
| --- | --- | --- |
| High WRAM `.bss` | 9,916 B | code and tables of `sat_music`, the disc scheduler, the stream runtime and the ADX/IMA decoders |
| Low WRAM `.wram_l` | 32,432 B | `g_audio_streams`: 4 SCSP stream slots, each with an 8 KiB upload staging buffer (33,184 B), linked as soon as any stream is opened |

That is before the player's own memory. `g_arena` (fighter_pack.c) is 734,672 B, sized for the worst pairing; the
tail a pairing leaves unused (`scripts/report_fight_arena.py`, 441 ordered pairings) is 0 B at the least (fighters 3
and 8), 134,880 B at the median. The player needs 116,000 B (scheduler 33,160 + workspace `2 * 16,384 * 2 + 17,416` +
its state) and leaves 194 of 441 pairings without room.

The ring cannot shrink. LibSaturn accepted 8,192 frames and stayed silent (`consumed` 0 for 235 s): the SCSP stream
starts when both its 4,096-frame halves are written, and the feed starts it only when no further whole segment fits.
`sat_music_open` now refuses a disc ring under 2 halves + one segment (12,288 frames for ADX). On Ymir, 12,288 plays but
underruns 106 times (minimum fill 885 of 12,288); 16,384 plays with none (minimum fill 2,048).

What the fight does with the drive: every disc read (packs, banks, stage, HUD, particles, flipbook, sounds) finishes
before `start_fight_theme()` ("READY"). A rematch or the next screen restarts the program through `sat_exec`
(`restart_program`), which now stops the stream first. Nothing reads the disc while a fight runs, so the scheduler would
own an idle drive; asset loading concurrent with music does not occur in the fight. It does in the menu, where the
select screen reads portraits into `g_io` (the test's arena), so `g_io` cannot host the player past the title.

The integration is written (`start_fight_music()` in main.c lends the arena tail, falls back to CD-DA when it is too
small, services the stream once a frame, stops it before `sat_exec`), off by default, and cannot be run:
the fight does not link with it. Sound RAM, per-frame cost and the transitions were therefore not measured in a fight.

To make it fit, memory has to leave the fight program or the library. Candidates, with sizes:
- `g_audio_streams` staging (33,184 B Low WRAM): the upload could read the ring directly in two pieces (the ring wraps
  once); no behaviour change, but it touches the stream path.
- 9.9 KB of High WRAM for the player's code and tables: something in the fight program has to give it up.
- 116,000 B for the player: free for 247 of 441 pairings; the other 194 would need CD-DA, or a fight-side change.

## Tests

`gmake test` in LibSaturn at the end of this work: exit 0, 60 host test programs report PASS plus the tool tests
(Python). It covers the asset cache and VFS reads without a cache (`test_file_asset_logic`: stats zero, EOF, partial
reads, chunked and stalling backends, prefetch `UNSUPPORTED`, a configured 4-block cache), the music API and disc
scheduler (`test_music_api`, `test_music_disc`: a disc track without a scheduler is `UNSUPPORTED`, a disc ring under
12,288 frames is `INVALID_ARG`), and the audio stream API. The SCSP key-on fix (58e5d149) is register-level: the host
tests mock the HAL, so no host test covers it; the evidence is the Ymir measurement (right channel 0 samples late, was
3). `ctest --preset host-tests` only runs 3 header tests and is not the suite. The cross-built `dino_demo` and
`cd_streaming_jukebox` examples link with the new rules.

## Fight memory after the library changes (WRAM-H, WRAM-L, Sound RAM, per component)

Library changes (lib-saturn, uncommitted until validated): the stream upload reads the caller's ring directly in at
most two pieces (no 8 KiB staging per slot); a disc-scheduler track needs no read buffer
(`SAT_MUSIC_DISC_WORKSPACE_BYTES`); the music, scheduler and stream-API units build with `-Os`. Host test: wrap-around
of an odd PCM8 and a PCM16 half, and a failed second piece consumes nothing; `gmake test` rc=0.

Ymir, menu ADX stream (90 s, after the change): 0 underruns, 0 rejected writes, minimum fill 2,048, 1,287 segments, 0
waits; right channel 0 samples late (was 0 before).

Fight program (`tekken3_saturn`), production build and the build with the player (`T3_FIGHT_ADX_MUSIC=ON`):

| Region | Production | With the player, before | With the player, now | Limit |
| --- | --- | --- | --- | --- |
| WRAM-H `.bss` end | 0x060fbf34 | the same code, before the library changes:, 26,604 B over the stack reserve (ld: 10,220 past the region) | 0x06101d3c (+23,868 B) | 0x060fc000 |
| WRAM-L `.wram_l` end | 0x002ffd10 | 32,432 B over | 0x002ffeb0 (+176 B) | 0x002ffe00 |

The whole WRAM-H excess is the player's code and tables (22.8 KB after `-Os`; 25.4 KB before): `sat_music` and
the disc scheduler 12.1 KB (api, disc_stream, disc_feed, open, player), stream API 4.0 KB, ADPCM decoders 4.7 KB (IMA
tables 2.1 KB are linked though the fight plays ADX), asset lookup and cache 3.2 KB, CD file system and block 0.7 KB,
SCSP 0.6 KB. The WRAM-L excess is `g_audio_streams` (4 slots, now 416 B).

Player memory in the arena (stereo ADX, ring 16,384):

| Component | Bytes |
| --- | --- |
| Scheduler `sat_disc_stream_t` (2 segment buffers of 16 KiB + state) | 33,160 |
| Two PCM rings (2 x 16,384 frames x 2 B) | 65,536 |
| Split and carry scratch | 9,224 |
| State (source, descriptors, handles) | 96 |
| Total (was 116,216 with the read buffer) | 108,024 |

Sound RAM: 0 B from the sample pool; the two stream slots use 32 KB of the 64 KiB stream area the profile already
reserves (pool 446,464 B; worst pair NINA vs OGRE leaves 10,308 B).

Arena tail per pairing (`report_fight_arena.py`): 108,024 B fit 269 of 441 pairings, 172 do not; the worst pair
(3 vs 8) has 0 B.

Residual deficit for the worst pairing: WRAM-H 23,868 B, WRAM-L 176 B + 108,024 B. Structures: the player's code in
WRAM-H (no dead code of that size was found in the fight: `g_replay_history`, `g_store`, `g_actors` and the
render buffers are in use during a fight), and for WRAM-L the arena, which holds the two animation banks (260-370 KB
each) and camera streams of the worst pair. Not done, and why: a ring smaller than 16,384 underruns; moving the
player's code to WRAM-L is not possible (`.wram_l` is not loaded from the image).

## ADX ring at 12288 frames with waiting segments (menu, Ymir, 90 s)

The disc scheduler now keeps an ADX segment in its drive buffer when the ring has no room, instead of dropping it
(lib-saturn 2319fcc2). Same menu run, ring per channel set by `T3_ADX_MUSIC_RING_FRAMES` (now overridable):

| Ring (frames) | Underruns | Rejected writes | Min fill | Segments | Consumed | Right-channel skew (60 s) |
| --- | --- | --- | --- | --- | --- | --- |
| 16384 (control) | 0 | 0 | 2048 | 323 | 4669440 | 0 samples, corr 0.869 |
| 12288 (candidate) | 0 | 0 | 2048 | 323 | 4673536 | 0 samples, corr 0.869 |

Before the change, 12288 gave 106 underruns in the same test. The menu is the only program that links with the
player: the fight still does not link (WRAM-H 23,868 B over), so the fight run and the 441-pair arena check remain
open. Mednafen recordings were not made; the Ymir videos were deleted after reading the audio.

## WRAM-H audit of the player, and what the library no longer links (fight, `T3_FIGHT_ADX_MUSIC=ON`)

`scripts/map_section_diff.py BASELINE.map ADX.map` lists the input sections the ADX fight has and the production
fight lacks (`.text`/`.rodata`/`.bss` in WRAM-H, by function section, largest first). Before the change the player
added 24,152 B of sections. Classified by what the ADX-from-disc path reaches:

| Class | Bytes | What |
| --- | --- | --- |
| Needed for ADX from the disc | 17,396 | `sat_music_open` 1.8 KB, player 2.0, disc feed (ADX deliver and decode, `next`, `started`) 2.0, disc scheduler 3.0, stream API (SCSP ring upload) 3.4, asset registry and mount (the track is registered as an asset) 2.5, cdfs/block/scsp 1.4, `adx_music.c` 0.8 |
| Not reached by ADX from the disc: IMA | 3,410 | decode tables 2,136, `decode_block` 488, `feed_adpcm` 436, `async_deliver_adpcm` 324, `block_bytes` 26 |
| Not reached: PCM | ~410 | `async_deliver_pcm` 208, `deinterleave_stereo` 180, `bytes_per_sample` 24 |
| Not reached: RAM and asset-reader feeds | ~1,400 | `feed_adx` 744, `feed` 544, helpers |
| Not reached: asset read and cache | ~1,800 | `asset_cache_fill` 808, `asset_cache_read_at` 760, `sat_asset_read_at` 292 (and the 384 B path normalizer is still needed by `sat_asset_open`) |

Why it was linked: `feed()` and `async_deliver()` chose the format with a branch, so the linker had to keep every
branch's code, and `sat_music_open` read the ADX header through `sat_asset_read_at`. The decoders, the tables and the
asset cache are reached only through the function pointers in one table now (`sat_music_codecs`,
`src/audio/music/codecs.cpp`); a program that streams ADX from the disc only defines that table with
`SAT_MUSIC_CODECS_ADX_FROM_DISC_ONLY` (`saturn/music_codecs.h`), and the linker leaves the library's out. Nothing else
in the program changes, the API is the same, and a program that never includes the header gets all formats as before.
`sat_music_open` reads the 4-byte header from the file's own reader.

Measured (lib-saturn e6663d2b; the fight program with the player, ring 16,384):

| Region | Before | After | Limit |
| --- | --- | --- | --- |
| WRAM-H `__bss_end` | 0x06101d3c (+23,868 B over) | 0x0610031c (**+17,180 B** over 0x060fc000) | 0x060fc000 (16 KiB stack kept) |
| Sections the player adds | 24,152 B | 17,396 B | |
| WRAM-L `.wram_l` end | 0x002ffeb0 (+176 B) | 0x002ffeb0 (+176 B) | 0x002ffe00 |

The fight still does not link. Menu, Ymir, 90 s, ring 16,384, with the narrowed table and the header read from the
file reader: 0 underruns, 0 rejected writes, min fill 2,048, 323 segments, consumed 4,669,440 (the same as before).
`gmake test` rc=0; `test_music_lean` plays ADX from the disc through the narrowed table and gets
`SAT_ERR_UNSUPPORTED` for S16, S8, IMA, a RAM track and another reader.

What is left in WRAM-H is the player: 17.2 KB of code is a measured floor for this architecture, with the pieces
above; no more is dead for ADX from the disc. It is not recovered from the fight's data by this change.

## ADX plays in the fight (resident arena, Ymir)

The VDP1 command banks moved to VRAM (78.7 KB of WRAM-H back), and the player now has its own arena in `.bss`
(`T3_ADX_MUSIC_RESIDENT`, `t3_adx_music_start_resident`), so every pairing gets the stream and the fighter-pack
arena tail is gone. The arena is 75,272 B: ring 12,288 frames per channel (the value measured clean in the menu
above) and the disc scheduler on 4-sector segments in caller storage (lib-saturn `sat_disc_stream_init_segments`;
the default two 16 KiB buffers would not fit). The fight with the player links with **3,308 B of WRAM-H** above the
16 KB stack reserve (`.bss` end 0x060fb314).

Attract fight, Ymir with cache emulation, 3,600 fields from boot (`scripts/ymir_fight.py --symbol g_t3_adx_music`);
the same AI pad hash 0x640e32c0 in every row, so it is the same fight. Images per second over the 1,360 observed
fields; the control is the same build with `T3_FIGHT_ADX_MUSIC=OFF`.

| Build | Images/s | Underruns | Min fill | Track played |
| --- | --- | --- | --- | --- |
| Control, no stream (CD-DA) | 31.81 | | | |
| 44.1 kHz, first link | 14.13 | 113 | 1,344 | 12.6 s |
| 44.1 kHz, decoder without `__ashiftrt_r4_12` | 16.63 | 116 | 1,344 | 15.0 s |
| 44.1 kHz, decode into the staging halves | 18.71 | 121 | 1,344 | 16.9 s |
| 44.1 kHz, music serviced while the Master waits for the Slave | 24.26 | 12 | 1,344 | 21.5 s |
| 22.05 kHz track, same build | 27.11 | 0 | 2,048 | 22.5 s |

What the profile showed (`saturn_probe.py --debug-profile`, 300 fight fields, `saturn_pc_profile.py functions`):

- The track is 44.1 kHz stereo (`ima_meta.h`), 88,200 samples a second, not 22,050 as section C of
  `MEMORY_LIFETIMES.md` assumed. In the first build ADX decode was 14.2% of the Master, delivery 5.3%, CD Block
  transfer 2.5%, at 4,670 cycles per stereo block: `disc_feed.cpp` is built `-Os` and inlined the header decoder,
  and GCC called `__ashiftrt_r4_12` for every sample's floor shift. The lib-saturn decoder now uses a biased
  logical shift (about 2,900 cycles per block; output unchanged against an exact reference).
- The Master spent about a quarter of its cycles waiting for the Slave (`master_receive`, `frt::counter`). The
  fight now marks the music service due and runs it in `simulation_join` when the Slave is still busy, else after
  the deferred join: once per frame either way. After that the Master's wait is about 1% and it is fully loaded;
  decode is then 17.2% of it at 44.1 kHz.
- At 44.1 kHz an image takes more than two fields; a slow image delays the SCSP refill, which is what the
  remaining 12 underruns are (the ring's minimum fill stays 1,344).

The 22.05 kHz file (`encode_adx_stream.py --rate 22050`, 5,111,824 B) halves the decode and the drive bandwidth
but costs quality: codec SNR 29.0 dB against 35.8 dB at 44.1 kHz, and nothing above 11 kHz. Neither rate reaches the
control's 31.8 images/s yet. Emulator measurements, not hardware; one pairing; no Mednafen recording was made.
