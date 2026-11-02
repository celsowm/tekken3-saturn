# Tekken 3 Arcade (C352) on the Saturn SCSP: feasibility, Paul Phoenix theme

Reproduce: `python3 -I scripts/check_arcade_rom.py ~/Downloads/tekken3` and
`python3 -I scripts/analyze_c352_vgm.py [--samples] <paul.vgz>`. The VGM is the VGMRips pack
"Tekken 3 (Namco System 12)", track 06 "Paul Phoenix", kept in the git-ignored `assets/imported/arcade-vgm/`.
No ROM data, samples or sequences are committed; this file holds aggregate numbers only.

## Verdict

**The theme cannot be moved to resident SCSP samples without degrading it.** Its 32 samples are 595,501 bytes
of 8-bit PCM (581.6 KiB), more than all of Sound RAM (512 KiB) and 149 KiB more than the 436 KiB that is
free for resident samples. Loading samples by section lowers the peak to 374.7 KiB, which fits but leaves about
61 KiB for the sequence, the fighter effects and any growth. The existing score facility
(1,024 raw register writes) is about two orders of magnitude too small, so a new 68000 sequencer is also
needed. A 10 s excerpt was built afterwards: `docs/ARCADE_C352_SCSP_PROTOTYPE.md`.

## Phase 1: ROM (measured)

- `~/Downloads/tekken3`: 15 files. `tet1wave0.5` and `tet1wave1.4` match MAME's SHA-1 exactly.
- All 21 data blocks in the VGM equal `wave0 + wave1` byte for byte at the VGM's own addresses (`wave1` follows
  `wave0`): the VGM uses these very sample ROMs.
- Revision: not established from contents. The program ROMs (`tet1vere.2e`/`.2j`, interleaved) are compressed and
  the only readable date is the BIOS build, "May 15 1997". The sound program is presumed to be `tet1verb.11s`
  (512 KiB, no readable text), not confirmed. MAME's program hashes were not available offline.

## Phase 2: the theme (measured from the VGM)

| Item | Value |
|---|---|
| Length | 130.44 s, loop starts at 54.8 s (the looped part is 75.6 s) |
| C352 output rate | 25,401,600 Hz / 288 = 88,200 Hz; VGM waits count 44,100 Hz |
| Register writes | 44,973 (mean 345/s, peak 667/s) |
| Key-on triggers (0x202 executions) | 10,638 writes; 3,061 notes started |
| Writes by register | vol_f 19,804; freq 7,169; flags 6,136; bank 198; start/end 337; loop 314; vol_r 40 |
| Voices used / max polyphony | 21 / 21 (first reached at 92.6 s) |
| Unique samples | 32 (19 looped, forward only; 5 mu-law = 39,689 bytes; no noise, reverse or ping-pong) |
| Referenced ROM bytes | 595,501 (non-overlapping; the VGM carries 595,667) |
| Playback rates | 347 Hz to 46,743 Hz; most of the big samples play at about 22,050 Hz (native) |
| Mean note length | 0.80 s (one voice holds 121.9 s) |

The VGM is a chip-command stream, not MIDI: the volume writes (19.8k) are sequencer envelopes that the
C352 slews in hardware, and they dominate the data.

## Phase 3: C352 to SCSP

| Topic | Mapping |
|---|---|
| Sample format | C352 8-bit linear maps to SCSP PCM8 unchanged. The 5 mu-law samples must be expanded to PCM16 (79,378 bytes, +39,689) or requantised to 8 bits (lossy). |
| Pitch | rate = 88,200 x freq / 65,536 Hz; SCSP pitch = rate / 44,100: ratio 0.008 to 1.06, inside the SCSP octave range (estimated, not tested on the chip). |
| Loops | forward loop to `wave_loop` with `wave_end`: SCSP LSA/LEA (PCM8 is byte addressed). Samples can cross a 64 KiB C352 bank; rebasing is offline. |
| Volume and pan | 8-bit L/R front volumes map to SCSP DISDL/DIPAN/TL; the C352's per-sample volume slew has no SCSP twin and becomes explicit steps (estimated). |
| Interpolation | C352 interpolates linearly; whether the SCSP matches was not measured. |
| Voices | The library keeps 4 for streaming: 28 resident voices, 21 for music, 7 left for fighter effects (measured from the library constants). |

## Phase 4: memory report

Measured:

1. Unique samples: 32.
2. Original ROM bytes referenced: 595,501.
3. PCM8 bytes after conversion: 595,501 (with the mu-law ones at 8 bits) or 635,190 (mu-law as PCM16).
4. Sound RAM: 0x3000 (12 KiB) is the driver, the top 64 KiB the stream area, so 0x3000-0x70000 = 436 KiB is
   resident. All 32 samples need 581.6 KiB: **over by 149 KiB**.
5. Max music voices: 21. Left for effects: 7.
6. Time-sliced loading (each sample resident from its first to its last note): peak 374.7 KiB, leaving about 61 KiB
   of the 436 KiB, before the sequence and any fighter effects.

Estimates:

7. Sequence: the 1,024-event score (8 B each) holds under 1.5% of this theme. One C352 write becomes 1 to 2 SCSP
   writes and a key-on about 9, so roughly 80k raw events, 650 KB. A compact note/volume bytecode is about 100-150 KB.
8. 68000 load: 345 writes/s mean (667 peak) is small for the 11.3 MHz 68000 (about 1-2k SCSP writes/s). The
   driver lacks a sequencer, not speed.
9. Swapping: required (see 6), sections loaded behind the playing one; samples are reused across the loop, so each
   pass reloads them.
10. CD traffic: one-time load 0.6 MB (about 291 sectors); with per-pass reloads at most about 5 KB/s over the
    75.6 s loop, against 44 KB/s for a 22.05 kHz stereo PCM8 stream. Sequence from the CD: about 1 KB/s.

## Alternatives (not built)

- **Section-wise sample banks plus a streamed bytecode sequence.** Fits only if the fighter effects and the
  driver can live in about 60 KiB; it needs the new sequencer and the mu-law decision. Cheapest change to test:
  measure the Saturn fighter effects' resident size first.
- **Render the theme offline from the VGM to 22.05 kHz PCM8 stereo** (about 5.7 MB per 130 s) and stream it, the
  path already built in LibSaturn. Its cost is Work RAM, not Sound RAM: the fight build has 752 bytes of Low and
  about 21 KB of High Work RAM free, so `sat_music` needs smaller rings first.
- **Resample the large samples** to fit. Rejected here: it changes the music; it would need a listening test.

## Not measured

Comparison with a reference rendering, timing drift, CPU load on a real 68000, and the Saturn fighter effects'
Sound RAM use. No emulator run was made for this study.
