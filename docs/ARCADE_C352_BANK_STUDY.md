# Arcade C352 on the SCSP, part 2: dynamic sample banks, no-streaming profile, sequencer

Follows `ARCADE_C352_FEASIBILITY.md`. Tools: `scripts/simulate_c352_sample_cache.py`,
`scripts/estimate_c352_sequence.py`, `scripts/analyze_c352_vgm.py`. Aggregate numbers only; the Paul VGM and the
ROMs stay outside git. Every figure is **measured** (from the code, ROM-derived tables or the VGM) unless marked
**estimate**.

Correction to part 1: the referenced sample bytes are 595,501 (the 595,561 quoted in the request is a typo). The
analyser also now ends one-shot notes when their sample runs out (before, only a key-off or re-key ended them);
note count, sample count and polyphony (21) are unchanged.

## 1. Real Sound RAM budget of a fight (measured from the sources)

The game creates every fight sound resident with `sat_sound_create` (`src/saturn/sfx.c`): the 25 shared sounds in
`SFX.BIN`, then both fighters' voice sets from their packs. The allocator is a sequential pool with 2-byte
alignment (`src/audio/playback/sound.cpp`), all sample sizes are even, so residency is the plain sum.

| Part | Bytes | Sounds |
|---|---|---|
| Shared effects and system sounds | 188,888 | 25 |
| Paul voices | 112,028 | 17 |
| Law voices | 99,680 | 17 |
| **Paul vs Law** | **400,596** | **59 of 64 sound slots** |
| Heaviest pair (Nina + Ogre) | 442,932 | 59 |
| Lightest pair (Jin + King) | 307,608 | 51 |

Sound RAM, from `src/hal/scsp` and `src/audio/streaming/runtime.hpp`: 512 KiB, minus 12 KiB for the driver,
minus 64 KiB for the stream area.

| Profile | Resident bytes | Free in Paul vs Law | Free in the lightest fight | Voices |
|---|---|---|---|---|
| Current (4 stream slots reserved) | 446,464 (436 KiB) | 45,868 | 138,856 | 28 resident |
| No streaming (not implemented) | 512,000 (500 KiB) | 111,404 | 204,392 | 32 |

The fight already fills 90% of the current Sound RAM; the heaviest pair leaves 3.5 KB. This was computed from
the allocator rules, not read from a running fight: no emulator run was made.

## 2. The music's working set (simulation over the real VGM events)

Rules: a sample is resident from the start of its load to the end of its last playing note, so no live voice
loses data. Notes whose volume is zero for their whole life are dropped (output-neutral; 3 long ones, 34,780-byte
sample, held 121.9 s at volume 0). Two passes simulate the loop. Placement is first-fit with 2-byte alignment.
CD model (**estimates**): 614,400 B/s (2x) and 0.4 s per transfer for the lead time.

| Eviction gap | Peak live | Extent with fragmentation | Loads per loop | CD bytes per loop | Average CD rate |
|---|---|---|---|---|---|
| 0 s (load per use) | 350,354 | 440,316 | 371 | 4.67 MB | 40.8 KB/s |
| 2 s | 350,354 | 433,312 | 75 | 1.50 MB | 13.1 KB/s |
| 5 s | 350,354 | 429,792 | 58 | 1.04 MB | 9.1 KB/s |
| 20 s | 371,756 | 416,882 | 30 | 0.50 MB | 4.4 KB/s |
| 60 s | 494,989 | 509,502 | 19 | 0.25 MB | 2.2 KB/s |

- Lower bound with instant loads: **330,872 bytes** (323 KiB) live at once, 400,210 with fragmentation. This is
  what the music itself needs; no cache policy goes below it without removing audible data.
- With the 5 mu-law samples expanded to PCM16 the bounds rise by about 10 KB (peak 360,243).
- Preloading before the fight costs nothing at run time but needs the full set; the 20 s row loads
  about 0.5 MB per loop once the first pass is in.

## 3. Verdict on music plus fight effects

Music needs at least 331,640 bytes and realistically 372,000 to 440,000 with fragmentation and load lead.
The fight effects need 307,608 to 442,932. Together they need 640,000 to 880,000 bytes, against 446,464 (current)
or 512,000 (no streaming). **Even the lightest fight does not fit in either profile, and the no-streaming profile
only moves the free space from 46 KB to 111 KB (Paul vs Law) against a music need above 331 KB.**

Voices: music peaks at 21; this leaves 7 (current) or 11 (no streaming) for the fight effects. This is the
smaller problem.

## 4. Sequencer (measured sizes, estimated cost)

- The original sequencer works in frames of 368 samples, 8.345 ms (about 119.8 Hz). There are 10,638 frames in
  the 130.4 s theme (each ends with the C352 key-on trigger).
- Compact bytecode (frame delta as a varint in 1.45 ms units, one op per touched voice: 3,061 key-ons of 6 bytes,
  17,676 volume changes of 3, 5,518 pitch changes of 3, 1,129 combined of 5): **114,877 bytes raw, 48,869
  bytes with zlib**, against 44,973 C352 writes that the 1,024-event score cannot hold (8 bytes each).
- Memory: resident it would cost 112 KiB of Sound RAM, so it should stream from the CD in small chunks:
  114,877 / 130.4 s = **881 B/s** (**estimate** of the needed buffer: 2 KiB).
- 68000 cost (**estimate**): about 100,000 SCSP register writes over 130 s (744/s mean, about 2,000/s peak); decoding
  a byte op is tens of cycles, so under 1% of the 11.3 MHz 68000.
- Time resolution. The driver tick is 256 samples (5.80 ms). Scheduling every real event on it gives a mean
  error of 1.45 ms and a **maximum of 2.90 ms** (measured on the event times); a 128-sample tick gives 0.73 / 1.45 ms,
  64 gives 0.36 / 0.73 ms. Events of one frame stay together at any tick. Whether 2.9 ms is audible was **not
  tested by listening**; a tick of 128 or 64 samples (689 or 1,378 interrupts/s) removes the question for little CPU.

## 5. Decision

The full Paul Arcade theme **cannot** be played by the SCSP together with the current fight effects, in either
profile, without removing audible music or effects. No proof of concept was built, since the numbers are not
favourable. The music alone (menus, results, a fight with its effects loaded on demand) is within budget:
331 to 440 KB of 446 to 512 KB.

Bottlenecks, in order: (1) the fight effects' 307 to 443 KB residency; (2) the music's simultaneous 331 KB;
(3) the missing sequencer, which is cheap (114 KB, 881 B/s, under 1% CPU); (4) CD seeks, 4.4 KB/s average
at the 20 s policy, mostly harmless but never measured on the drive.

What would change the answer, not done here: load a fighter's effects in groups instead of all 59 up front
(needs a usage count per fight); or reserve the music for screens without combat. Both change the game and were
not attempted. The existing CD-DA path is untouched.

## Not measured

A real fight's allocator state, the CD drive's actual seek times, the audibility of the 5.8 ms tick, the SCSP's
interpolation compared with the C352, and the 68000 cycle counts.
