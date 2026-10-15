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

**No download occurs during build or CI.** The URL is only used upon explicitly running this command. It has not been verified that the external ZIP remains downloadable; for an already-obtained ZIP/BIN/ISO, pass its local path to `--source`. Python 3 standard library is sufficient.

Generated files are excluded by Git: `inventory.json` for all 303 entries; `raw/036.bin` (stage ARC textures, still opaque), `raw/056.bin` (panorama), `raw/071.bin` (first documented fighter KMD), and `stages/stg_a.obj` with untextured panorama vertices and faces. The OBJ is an **intermediate research artifact**, not yet a runtime-ready Saturn asset or a faithful on-screen panorama.

Next: decode stage TIM assets from corresponding ARC records, map CBA/TSB/UVs, adapt for Saturn VDP1/VDP2 budgets; parse KMD skin sections and divmot motion data, and validate frame-by-frame. There are no original models/animations in the executable at this stage.

References: [independent Tekken3Remake format research](https://github.com/n0madic/Tekken3Remake/tree/master/docs/research/formats), [Tek3Ex](https://github.com/xan1242/Tek3Ex). The implementation here was independently written and does not copy those tools' source.
