# Paged 68000 sequencer for the C352 to SCSP work

A paged score for the resident SCSP sound driver (driver version 3), a converter that writes pages, and a dense
10 s window of the Paul Phoenix theme (3,095 events) played on Ymir from the 68000 with no late, lost or
overwritten event. Not integrated into the Tekken 3 build; no push. The driver part lives in LibSaturn
(`lib-saturn` commit `8a838779`: `sat_sound_driver_pages_*`, `examples/sound_driver_pages_demo`), so it stays
useful whatever happens to the Tekken 3 music.

Private data (ROM-derived bank, pages, captures, WAVs) is under the git-ignored `assets/imported/c352-proto/`.

## 1. Toolchain

`brew install m68k-elf-binutils` (2.47). `python3 tools/build_sound_driver.py --check` in LibSaturn passes on the
checked-in image before the change and `python3 -m unittest tests.tools.test_sound_driver` passes after it.

## 2. The paged score

The driver's 8 KiB score area (0x1000 to 0x3000) is two pages of 4 KiB, 512 events each. Nothing was added to the
global Sound RAM reservation: the driver still ends at 0x3000, the new state is 0x50 bytes of the mailbox that was
unused (0x240 to 0x290), and the code grew from 648 to 930 bytes (the code area ends at 0x800).

| Requirement | How it is met |
|---|---|
| Absolute or page-relative timestamps, no drift | Event time = stream base + page time base + event tick, all 32-bit absolute against one base. Time base 0 is absolute; a page rebased to its first event is relative. Both were run (relative at 256 samples, absolute at 128). |
| Two pages, continuous hand-over | Page `n` is in slot `n & 1`. When the last event is read, the 68000 frees the page and goes on to the next one inside the same interrupt. |
| Atomic publication by the SH-2 | Data, count, time base and end time are written first, then the tag (page number + 1) in a single word write. The 68000 only reads a page whose tag is the number it expects. |
| Consumed only by the 68000 | The SH-2 never executes an event; it copies a page (about 4 KiB, once per 1.2 to 1.7 s here). |
| No overwrite of an active page | The 68000 clears the tag when it has finished with a page; `page_publish` returns `SAT_ERR_BUSY` while the tag is set, and `pages_info` shows the free count. A deliberate publish into a busy page at frame 200 was refused (result `BUSY`, `refused` = 1). `bad_sequence` counts pages seen out of order: 0. |
| Missing events, lateness, underrun | `underrun_ticks` counts ticks in which the next page was missing after the previous one's end time (the end time is the first event of the next page); `late_events` and `max_lateness_ticks` count events run after their tick. |
| Old 1024-event mechanism | Unchanged; the two modes exclude each other (starting one stops the other). A legacy run on the new driver executes the same 488 events with lateness 0 and an audio capture equal to the earlier one in all but 501 of 882,000 samples (a 6-sample start shift, rms difference 0.9 on a signal of rms 2,288). |
| Continuity of voices and envelopes | The driver resets nothing at a hand-over. The converter's slot plan is page-blind (a slot keeps playing across pages). Measured: see 4. |

### The 128-sample tick

Timer A only counts 256 and wraps to 0 in the Ymir model (a preload shortens the first period only; the first
attempt ran at half speed). A reload on each interrupt would add the interrupt latency to every period and
drift. The 128-sample mode therefore starts timer B 128 samples after timer A, both free-running at 256, and
counts every interrupt as a tick. The grid cannot drift: 128-sample and 256-sample runs of the same stream ended
at video frames 617 and 616. Only 256 and 128 are accepted. 64 was not tried: the SCSP has three timers, which
cannot be staggered by 64; the onset error would go from 0.72 ms to 0.37 ms for one tick above the clean
capacity in this window.

## 3. The window: 15 s to 25 s of the theme

`scripts/c352_scsp_convert.py --start 15 --end 25` (the full band enters at 15 to 16 s).

| | |
|---|---|
| C352 register writes | 3,360 |
| SCSP events | 3,095 at 256 samples (3,090 at 128), in 7 pages |
| Notes | 316, of which 312 converted; 9 were already sounding at 15 s and enter at the sample position the C352 had reached (a loop enters at its loop start) |
| Peak polyphony | 14 voices; 32 slots used (279 notes reuse a slot that already holds their sample) |
| Changes | 942 volume (200 move the balance), 1,228 pitch |
| Samples | 9 (one mu-law expanded to PCM16), 219,474 bytes, all loaded before playback |
| Busiest tick | 72 events (256 and 128 samples) |

For 10 s windows from 2 s to 20 s the converter gives 579 to 3,300 events; the 1,024-event score fits only
windows that start in the first ~5 s.

## 4. Results on Ymir

| | 256 samples (control) | 128 samples |
|---|---|---|
| Events executed | 3,095 of 3,095 | 3,090 of 3,090 |
| Late events / worst lateness | 0 / 0 ticks | 0 / 0 ticks |
| Underrun ticks, pages out of order | 0, 0 | 0, 0 |
| Pages published / played | 7 / 7, stream done | 7 / 7, stream done |
| Page margin at publication (first event, in ticks) | 52 to 339 (0.3 to 2.0 s) | 103 to 659 (0.3 to 1.9 s) |
| Most events in one tick | 72 | 72 |
| Pages overwritten before consumption | 0 | 0 |
| Voices owned by the SH-2 mid-playback | 0 | 0 |
| Onset error against the C352 time (mean / max) | 1.39 / 2.88 ms | 0.72 / 1.45 ms |

Continuity at the six hand-overs of the 256-sample run (`scripts/check_page_continuity.py`, 200 ms windows, capture against the model
of what the converter wrote): correlation 0.959 at hand-overs against 0.879 elsewhere, so nothing is lost or
delayed there.

The SH-2 publishes a page when `pages_free` is non-zero and never touches an individual note. The prototype
program draws four text lines and calls no audio update.

### Throughput of the 68000 (`scripts/stress_sound_driver_pages.py`)

A stream with N harmless register writes in every tick for 240 ticks (of 256 samples), two audible clicks at the
ends; if a tick's events do not fit in a tick, interrupts merge and the clicks end up further apart than their
tick count says.

| Tick | Events per tick | Result |
|---|---|---|
| 256 samples | 64, 96, 128, 144, 160, 176 | clean (no late event, clicks exact) |
| 256 samples | 192 / 256 / 320 | late: 2,640 / 13,645 / 25,913 events; ticks lost: 3 / 52 / 123 |
| 128 samples | 48, 64, 80 | clean |
| 128 samples | 96 / 128 / 192 / 256 | late: 1,447 / 6,104 / 18,289 / 28,911; ticks lost: 6 / 64 / 344 / 612 |

So the 68000 runs about 30 events per millisecond (about 370 cycles per event, ISR included). The dense window's
busiest tick is 12 events/ms at 256 and 25 events/ms at 128, which leaves about 20 % headroom at 128 samples.

### Memory

| | |
|---|---|
| Sound RAM, sample bank | 219,474 bytes (49.2 % of the 446,464-byte pool) |
| Sound RAM, driver and score area | 12,288 bytes (0x3000), unchanged; the paged score uses the existing 8 KiB |
| Sound RAM, driver code | 930 bytes (was 648) |
| SH-2 Work RAM, prototype image | 281,392 bytes: bank 219,474, pages 24,872, the rest code and data; no page buffer |
| SH-2 Work RAM, `sound_driver_pages_demo` | 4 KiB page buffer, generated pages |

A page that comes from the CD would need two 4 KiB buffers on the SH-2 at most.

## 5. Fidelity against the C352 oracle

Raw gains are the C352's own: `--scale 1.0` plays the volumes as written. The loudest passages then exceed 16
bits, and the SCSP clips: raw run RMS +6.06 dB against the oracle (libvgm's output is about 6 dB lower), 2,669
clipped samples. The normalised runs use one global factor 0.5 (-6.02 dB, not fitted to the file). Windows are
compared from 15 s of the oracle; the capture is aligned to the first sound.

| Metric (scale 0.5) | 256 samples | 128 samples |
|---|---|---|
| RMS, SCSP against oracle | +0.10 dB | -0.29 dB |
| Peak | -0.28 dB | -0.60 dB |
| Octave bands 62 Hz to 16 kHz | within -0.31 to +0.16 dB | within -0.31 to -0.09 dB |
| 16 to 22 kHz | +5.0 dB | +5.1 dB |
| Left/right, per second (max difference) | 0.38 dB | 0.38 dB |
| Waveform correlation (mean per second) | 0.50 | 0.70 |
| 10 ms envelope correlation | 0.80 | 0.92 |
| Envelope error, standard deviation | 3.1 dB | 2.0 dB |
| Spectrogram cosine | 0.82 | 0.82 |

### Where the difference comes from (`scripts/attribute_c352_scsp_error.py`)

Each effect is added to a Python model, one at a time, and compared with the oracle (waveform correlation /
envelope correlation / spectrogram cosine), 256 | 128 samples:

| Stage | 256 samples | 128 samples |
|---|---|---|
| A. C352 rules at 44.1 kHz (resampling, interpolation, neglected 1.9 ms volume ramp) | 0.96 / 0.90 / 0.95 | same |
| B. + pitch rounded to OCT/FNS | 0.92 / 0.90 / 0.86 | same |
| C. + events on the tick grid | 0.53 / 0.75 / 0.82 | 0.75 / 0.88 / 0.83 |
| D. + level and pan steps, mid-note entry | 0.53 / 0.75 / 0.81 | 0.75 / 0.88 / 0.82 |
| Capture against the oracle | 0.50 / 0.80 / 0.82 | 0.70 / 0.92 / 0.82 |
| Capture against model D | 0.90 / 0.93 / 0.86 | 0.91 / 0.88 / 0.84 |

- **Pitch:** the rounding costs at most 0.74 cents (mean 0.33). The peak-picking pitch error against the oracle
  rises from 1.5 to 15 cents only when the tick grid is added, and it does so with exact pitch too: the measure
  reads beating between detuned pad voices, which moves when their starts move. It is a timing effect, not a pitch
  error.
- **Temporal quantisation:** the dominant cause of low waveform correlation. 128 samples recovers about two
  thirds of the loss (0.53 to 0.75).
- **Interpolation / resampling (A):** 0.96 waveform correlation, +4 dB above 16 kHz. This is not removable.
- **Gain and pan:** TL/DISDL/DIPAN steps and mid-note entry cost almost nothing (0.53 to 0.53); the left/right
  ratio matches within 0.38 dB per second.
- **Key-off and loop** (`c352_scsp_timing_probe.py keyoff-*`, a looped 100 Hz sine): the loop comes out at exactly
  100.000 Hz over 6 notes (LEA exclusive is right); the SCSP release (RR 31) takes about 50 samples (1.1 ms)
  against the C352's immediate cut. The loop seams in the Paul window are continuous in the data.
- **What is left between the capture and model D** (0.90 waveform correlation): the SCSP's own envelope,
  interpolation and DAC, and the model's simplifications.

Listening files: `assets/imported/c352-proto/dense256/compare/` and `dense128/compare/` hold
`reference_44100.wav`, `scsp_aligned.wav` and `scsp_gain_matched.wav` (the capture scaled to the oracle's RMS);
`dense256-raw/compare/scsp_aligned.wav` is the clipped raw-gain capture. I did not listen to them; recognisability
is not judged here.

## 6. Reproduce

```
export PATH=/Users/celso/dev/saturn-tools/bin:$PATH
python3 scripts/run_c352_scsp_prototype.py --vgm assets/imported/arcade-vgm/paul.vgz \
  --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav --start 15 --end 25 --scale 0.5 \
  [--tick-samples 128] [--relative-pages] --out assets/imported/c352-proto/dense256 --keep-audio
python3 scripts/attribute_c352_scsp_error.py ... ; python3 scripts/check_page_continuity.py ...
python3 scripts/stress_sound_driver_pages.py --out assets/imported/c352-proto/stress
python3 -m unittest tests.test_c352_scsp_convert     # LibSaturn: tests.tools.test_sound_driver
```

## 7. Limits that remain

1. The pages can stream from the CD (section 8), but samples are not loaded dynamically: the whole bank is
   resident before playback.
2. The 128-sample tick leaves only 10 to 30 % headroom on this window's busiest tick; a denser passage or a slower
   event could need a smaller burst limit, a spread-out slot setup or the 256-sample tick.
3. Tick quantisation (up to 2.9 ms, or 1.45 ms at 128) and FNS pitch rounding remain; exact timing and pitch
   would need a finer clock or resampled data.
4. Waveform correlation with the oracle stays below 0.8 for the dense window; spectrum, level, pan and envelope
   agree much better. Whether it sounds like the arcade is a listening question.
5. Notes already sounding at the start of a window enter at a guessed phase when they loop.
6. Only the 128-sample burst limit was measured at the register-write mix of the stress test; real events
   (key-ons) may cost more per event on the SCSP bus.
7. The underrun counter was exercised only by overload (the stress streams that exceed 30 events/ms count 24 to
   166 underrun ticks); a page published late on purpose, and a forced reuse race, were not tested.
8. One window of one track, Ymir only.

## 8. Pages and bank from the CD

`run_c352_scsp_prototype.py --cd` builds the program with `C352_CD=ON`: the image holds only code, and a disc
holds `BANK.BIN` and `PAGES.BIN`. The program mounts the disc, reads the bank in 64 KiB pieces into
`sat_sound_create`, checks it by a word checksum against Sound RAM, then reads the pages one at a time: page 0
before the stream base is set, and each next page right after the previous one is published, so one page waits
in RAM while the 68000 plays (128-sample tick, same 15 s to 25 s window, run on Ymir):

| | From the image | From the CD |
|---|---|---|
| Events executed, late, underruns, out of order | 3,090, 0, 0, 0 | 3,090, 0, 0, 0 |
| Capture against the oracle (RMS, envelope correlation) | -0.29 dB, 0.92 | -0.29 dB, 0.92 |
| Program image (SH-2 Work RAM) | 281,392 bytes | 41,936 bytes |
| Bank load | in the image | 219,474 bytes in 237 ticks (0.69 s), checksum equal |
| Page reads | none | 7, longest 50 ticks (0.145 s), total 281 ticks (0.82 s) |
| Page margin at publication | 103 to 659 ticks | 103 to 664 ticks |

The SH-2 blocks for the length of a page read (a frame or nine), the 68000 does not notice. The CD runs on
Ymir's block model, so the read times are a measurement of that model, not of a drive. Reading pages is the only
CD traffic during playback; nothing else may touch the drive then. Samples are still loaded as a whole bank
before playback.

## 9. Next

- Load samples a section at a time while the pages play (dynamic bank), with the converter writing one bank per
  section; pages already stream from the CD.
- Decide on the 128-sample tick per song section from the burst profile the converter prints.
- Provoke an underrun (a late page) and a page-reuse race on purpose, to test the detection in isolation.
