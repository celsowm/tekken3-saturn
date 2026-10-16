# Tekken 3 USA local-disc import

The game disc image is user-supplied **input**, not part of Git, the Saturn executable, submodule, or GitHub Actions. The importer does not need the Tekken3Remake submodule to run.

The supported release is **SLUS-00402**. Its `TEKKEN3/SLUS_004.02` executable must have SHA-256 `fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c` before its 303-entry BNS table is trusted. The parser handles the CD-ROM 2352-byte Mode 2 Form 1 data track (2048 bytes at offset 24) and cooked ISO 2048 files. A ZIP may contain several tracks: the tool uses Track 1, not the audio tracks.

## Offline import on a disk with space

```sh
python3 scripts/psx_disc_import.py \
  --source 'https://archive.org/download/tekken3_playstation/Tekken%203%20%28USA%29.zip' \
  --cache /mnt/big-disk/tekken3_usa.zip \
  --scratch-dir /mnt/big-disk/tmp \
  --output assets/imported/tekken3-usa \
  --extract-ids 36,71,73 \
  --stage-ids 56
```

**No download occurs during build or CI.** The URL is only used upon explicitly running this command. `--source` also accepts the Internet Archive ZIP listing, for example `https://ia601000.us.archive.org/view_archive.php?archive=/33/items/tekken3_playstation/Tekken%203%20%28USA%29.zip`; the importer resolves it to the public ZIP download. For an already-obtained ZIP/BIN/ISO, pass its local path. Python 3 standard library is sufficient. Cached files must be valid ZIPs; incomplete downloads are not promoted to the cache.

## Discover original move banks

Add `--divmot-banks` to scan all 303 BNS records and validate each recognized
DIVMOT directory through the existing compactor. This writes private
`divmot/{BNS-record-ID}/bank_{type}.t3logic` and a SHA-256 manifest, preserving
motion, attack geometry and frame events. Separate BNS records are kept
separately even when their internal bank types match. `inventory.json` lists
the discovered bank types, original record IDs and generated paths.

```sh
python3 scripts/psx_disc_import.py \
  --source roms/tekken3_usa.zip \
  --divmot-banks --dump-exe \
  --extract-ids 71 --stage-ids 56
```

This validates bank structure and prepares authored data for the host tools;
animation arithmetic, original-game frame parity and Saturn performance
still require separate checks. These outputs remain under gitignored
`assets/imported/` and are never used by default public builds.

The supplied USA ZIP has been imported locally with the expected executable
fingerprint: 303 BNS records, 73 valid DIVMOT records covering internal types
0–20 and the shared type 99, plus 104 structurally valid original executable
input sequences. The compact banks total 20,536,108 bytes with motion,
hitboxes and events. This is an offline collection for analysis; the whole
collection is not a Saturn resident-memory budget or a playable build.

Generated files are excluded by Git: `inventory.json` for all 303 entries; `raw/036.bin` (stage ARC textures, still opaque), `raw/056.bin` (panorama), `raw/071.bin` (first documented fighter KMD), and `stages/stg_a.obj` with untextured panorama vertices and faces. The OBJ is an **intermediate research artifact**, not yet a runtime-ready Saturn asset or a faithful on-screen panorama.

Next: decode stage TIM assets from corresponding ARC records, map CBA/TSB/UVs, adapt for Saturn VDP1/VDP2 budgets; parse KMD skin sections and divmot motion data, and validate frame-by-frame. There are no original models/animations in the executable at this stage.

References: [independent Tekken3Remake format research](https://github.com/n0madic/Tekken3Remake/tree/master/docs/research/formats), [Tek3Ex](https://github.com/xan1242/Tek3Ex). The implementation here was independently written and does not copy those tools' source.
