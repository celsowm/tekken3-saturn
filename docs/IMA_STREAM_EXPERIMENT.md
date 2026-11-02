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
right channel carries most of the loss in the overall figure. The skew is older than the audio split (2 samples
on 771b9af6). The one-sample change comes with the start. The left channel's phase also moves with the start
(+0.36 samples on 771b9af6, −0.20 on 38410b44, each steady to 0.03). Neither the decode nor the streaming code
changed, so the cause of the skew is in key-on and playback start (`start_streams`, `arm_key_on`) or in the
emulator's SCSP model. Not determined; the fix, if it is in libsaturn, is in the start path, and it was not
made.

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

- **Slots.** The stream takes SCSP slots 28–31 (LibSaturn's stream reservation). The effects path uses 26–31. A
  fight audio change has to renumber one of them. Not done here.
- **Loop.** The track wraps at 206.07 s. The scheduler's `music_loops` counter stayed 0 across the run, so the
  wrap is not confirmed by the counter. The capture was not inspected at the wrap.
- **Time.** `ticks_per_second` read as 0 (the frequency was requested before the timer was set up), so the
  scheduler's tick counts (`max_ticks_poll` 1,103) are not converted to time.
- **Hardware.** Ymir's CD model is not a real drive. The 44 KB/s average is well under single speed, but seek and
  latency were not measured.
- **Listening.** Nothing was heard. The numbers above are the only evidence.
