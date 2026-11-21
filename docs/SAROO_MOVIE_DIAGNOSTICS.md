# SAROO movie diagnostics (real Sega Saturn)

## Symptom and control

On a physical Saturn/SAROO setup, a recording showed multicolour video noise and
audio hiss. This is not evidence by itself that either the CD Block or the HDMI/SCART
chain is responsible. Use the **same LSINTRO.MOV** from LibSaturn in both its
`examples/movie_player` and this game's `INTRO.BIN` as the controlled comparison.
The game's original `INTRO.MOV` is a *different* longer movie and should be tested
only after the shared logo clip.

## One-command macOS build

From an existing checkout containing a configured private Sega Saturn build:

```sh
cd ~/dev/tekken3-saturn && git pull --ff-only origin main && bash scripts/qualify-saroo-movie-mac.sh
```

The script pulls `main` in **both** Tekken and LibSaturn, rebuilds/installs the
matching SDK, and reconfigures the user's existing private Saturn build with
`T3_MOVIE_DIAGNOSTICS=ON` and `T3_TEST_SKIP_INTRO=OFF`. It then builds the game,
runs an offline checksum regression, computes the expected movie signatures, and
copies the generated CUE/BIN into a timestamped directory under
`build/saroo-movie-result/`. Nothing is uploaded.

If the automatically selected private build is not the intended one:

```sh
T3_BUILD_DIR=/absolute/path/to/existing/private/build bash scripts/qualify-saroo-movie-mac.sh
```

Optional environment overrides: `LIBSATURN_SOURCE_DIR`,
`LIBSATURN_PREFIX`, `LIBSATURN_BUILD_DIR`. The installed SDK is overwritten
at the selected **local** prefix, so use a dedicated prefix if another project
must remain on an older SDK revision. The generated game disc can include
retail-derived assets: keep it private and do not publish the CUE/BIN or zip it
for public sharing.

## What to photograph on SAROO

1. Boot the generated `.cue` on the actual console and let `LSINTRO.MOV` finish
   **without pressing START**.
2. Photograph the report displayed after playback: `STATUS`, `PACKETS`,
   `HASH`, `CD ERR`, `PKT ERR`, `VIDEO ERR`, `AUDIO ERR`,
   `ERR FRAME`, `ERR CODE`, `STARVED`, `SKIPPED`.
3. Compare `PACKETS` and `HASH` (both decimal on-screen) with the first
   line for `LSINTRO.MOV` in `expected-hashes.txt` generated on the Mac.
4. Press and release START to continue to the Tekken movie, when present.
   Photograph its report separately.
5. Repeat using LibSaturn's standalone `movie_player` example, **same console,
   SAROO, SD card and A/V chain**. Its CPU/DMA/FLIP diagnostic pages help isolate
   NBG0/VRAM behavior if the stream signatures match.

The hash is a low-overhead diagnostic, not a cryptographic digest. It covers every
accepted movie packet's original bytes (audio, video, padding). If `PACKETS`
or `HASH` differs from the host reference, investigate CD sector reads,
memory overwrites, CUE/data placement and the stream ring. `CD ERR` counts
non-OK calls to `sat_cd_stream_poll`. `PKT ERR` detects invalid lengths or
truncation. `VIDEO ERR` and `AUDIO ERR` cover decoder/SCSP errors.

If both signatures **match** even though there is video noise and audio hiss,
the consumed movie data matched the Mac reference (subject to checksum collision),
so investigate VDP2 DMA/bank flips, SCSP playback, program reset/state and
cabling/scaler next. Matching signatures do **not** prove the overall playback
path is correct.

`T3_MOVIE_DIAGNOSTICS` defaults to OFF for normal builds; report pages run only
when opted in. The player now returns transport and packet faults instead of
indefinitely confusing them with temporary buffering.
