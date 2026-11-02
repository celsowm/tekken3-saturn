# Nina vs Ogre: the 460,684-byte report was an overestimate

`scripts/report_fight_sound_budget.py` said the heaviest pair, Nina and Ogre, needs 460,684 bytes of Sound RAM for
a 446,464-byte pool (14,220 over). The game loads that fight. This note says which of the four possible causes
it is and what the real figure is. Nothing in the fight or in the effects was changed.

## Cause

| Hypothesis | Verdict |
|---|---|
| 1. An estimate that is not the simultaneous residency | **Yes, in part.** The report summed every row of the shared-effect table. |
| 2. An allocator limit that makes loading fail | No. No load failed and the program did not stop (see the run). |
| 3. Duplicated samples | **Yes, and it is the whole difference.** Three table rows repeat an earlier row's offset and size. `t3_sfx_init` (`src/saturn/sfx.c`) gives such rows one sound and one copy (`g_sound[i]=g_sound[same]`); the report counted each row. |
| 4. Report and real build differ in configuration | No. The report reads the same generated table and roster the build uses, and its corrected figure equals the measured one to the byte. |

The shared table has 28 rows and 25 distinct (offset, size) pairs: 206,640 bytes counted, 182,112 resident, 24,528
counted twice. The report now counts the distinct pairs.

## Measurement on Ymir (a running build)

A test build with `T3_BOOT_FIGHTERS="5;14"` (Nina, Ogre), run 2,400 fields from boot, the program's own counters
read through `scripts/saturn_probe.py`:

| Counter | Value |
|---|---|
| `sound_ram_used` | 436,156 |
| `sound_ram_high_water` | 436,156 |
| `sound_ram_capacity` | 446,464 |
| resident sounds | 59 of 64 |
| voice steals / failed plays | 0 / 0 |
| sounds played in the run | 55 |
| program panic | none |

436,156 = 182,112 (shared) + 127,652 (Nina's 18 voices) + 126,392 (Ogre's 16 voices). That is also what the
corrected report prints. Free pool: **10,308 bytes (2.3%)**, not −14,220. The pair that was reported 3% over fits,
with no margin.

## Consequences

- The fight's Sound RAM figures in `docs/ARCADE_C352_BANK_STUDY.md` section 1 counted duplicate rows (and an
  earlier table): today Paul vs Law is 393,820 (52,644 free), Nina vs Ogre 436,156 (10,308 free), a mirror match
  238,896 (207,568 free). The bank study's conclusion does not change: the music's 360 KB working set does not fit beside any pair
  but a mirror match, and not even then with a 5% margin (238,896 + 360,248 = 599,144 against a 446,464 pool).
- The slot limit is not the constraint: 59 of 64 sound slots for the heaviest pair.
- The margin is thin: any new shared effect or a longer voice set for Nina or Ogre breaks the heaviest pair. The
  report is the check to run first.

Reproduce: `python3 -I scripts/report_fight_sound_budget.py [--pair NINA OGRE]`; the run is
`cmake -DT3_BOOT_FIGHTERS="5;14"`, build, `scripts/saturn_probe.py --disc build/disc/music.cue --frames 2400`, then
read `sfx-ram-used.bin` and its neighbours from the output folder. Reset `T3_BOOT_FIGHTERS` afterwards.
