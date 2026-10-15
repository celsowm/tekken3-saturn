# KMD / 3DMK to LibSaturn SKA1: experimental first milestone

The first converter bridges the **original PS1 KMD character geometry** to the
**public LibSaturn skeletal compiler**. The parser is independent and
accepts only a locally extracted, user-supplied KMD file, such as record 71
(`paul3.kmd`, costume 1), record 75 (costume 2), or another character model.

It reads 27 original KMD rows, vertex/normal/face/material blocks, 22
mapped draw parts, parent-relative translations, and the PS1 scratch/global
vertex seam exchange. Seam vertices become **multi-anchor vertices** in
LibSaturn's `SourceModel.skin_anchors`, not rigid per-part geometry.
The original game keeps 27 rows but renders 22 parts; the second geometry
rows of parts 1, 2 and 17 are emitted with the same joint.

Run locally (no game assets are checked in):

```sh
git submodule update --init --recursive
python3 scripts/psx_disc_import.py \
  --source '/path/to/Tekken 3 (USA).zip' \
  --extract-ids 71
python3 scripts/kmd_to_ska.py \
  --kmd assets/imported/tekken3-usa/raw/071.bin \
  --libsaturn-source /path/to/libsaturn \
  --output assets/imported/tekken3-usa/paul
```

Output: `fighter_bind.ska` (native LibSaturn SKA1 archive),
`fighter_bind.obj` (untextured, bind-pose preview),
`manifest.json` (counts and SHA-256). The archive has **ONE static 'bind'
clip, an ORIGINAL neutral gray 8×8 placeholder texture** (not PS1 textures),
and the original mesh's source geometry, including up to three weighted
joint-local anchors. The public LibSaturn SKA1 encoder and skin validator
are used rather than a new binary format.

**Unfinished/known limits:** the KMD coordinate basis/origin and face
winding still require visual comparison to an original-game reference.
Costume attachments (parts 18–21) are conservatively excluded until
the EXE enable table is decoded. The 41 possible hand/jaw variants are
counted but not exported. Flat/Gouraud colors and TIM/ARC textures remain
placeholders, and DIVMOT motion clips have not been converted. The
source's scale-percent field is reported; runtime physical scale and
orientation are not yet calibrated. A separate, explicit `-DT3_LOCAL_KMD=...` option can now embed the
local archive into the Saturn game for a **private static visual test**.
The original-asset fighters remain the default, and CI never reads
commercial game data. The preview is not motion-accurate.

The independent [n0madic format research](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/formats/3dmk-models.md)
is the source for the documented PS1 record offsets, not copied code.
The Tekken3Remake submodule retains its own PolyForm Noncommercial license.
