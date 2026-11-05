# Arcade mode

ARCADE MODE is the original's one-player run: ten fights against a drawn
ladder of CPU opponents, CONTINUE? after a loss, True Ogre after the tenth
fight's first round, then a closing screen. VS MODE is the two-player match.
The rules come from the pinned Tekken3Remake research
(`docs/research/code/modes.md`, ArcadeBuildLadder `0x800B2494`, and
`FUN_800502D8` for the continue screen). They are adapted in C with attribution.

| Part | Where | Rule |
|---|---|---|
| Ladder | `src/game/arcade_ladder.c` | Fights 1–4 draw from the ten original characters (ids 0–9). Fights 5–8 draw from everyone else left, without Heihachi, Ogre or True Ogre. Fight 9 is Heihachi (Jin for a Heihachi player). Fight 10 is Ogre. Each draw is `x = x·5 + 1`, `pool[x mod count]`, and the pool's last entry fills the gap. |
| CPU level | `t3_arcade_level` | The fight index, 0–9. |
| Pick | `menu_main.c` `select_flow` | Player 1 picks only their fighter, even with a second pad plugged in. The VS screen shows `STAGE n` and the drawn opponent, and the fight uses the opponent's home stage and music. |
| Next fight | `menu_main.c` `main` | After a win the menu skips the select and goes straight to the next VS screen, as FightMain's sub-state 2. |
| Continue | `main.c` `ladder_step` | `CONTINUE? n` runs for 808 fields and shows `(left + 90) / 90`. A face button takes 90 fields off. START retries the same fight with the same fighter. |
| Game over | `ladder_step` | `GAME OVER` for 121 fields, then the title. |
| True Ogre | `ladder_step`, `psx_match` `start_wins` | At fight 10, once the player wins a round against Ogre, the fight reloads against True Ogre and keeps both sides' round wins and the rounds played. |
| Ending | `intro_main.c`, `menu_main.c` `ending_flow` | After fight 10, INTRO.BIN plays the winner's ending movie, `ENDnn.MOV`. START skips it. Then a closing screen with the winner's name plate, and the title. Doctor B. and True Ogre play Yoshimitsu's and Ogre's endings, as in the original's lookup table. |
| New challenger | `ladder_step` | START on the other pad shows `A NEW CHALLENGER ENTERS!!` for 66 fields, then both players pick and fight a VS match. If port 2's player wins, they carry the run on from the same fight on the swapped pads. If port 1's player wins, the run goes on as before. A draw repeats the match. |

## Ending movies

`scripts/pack_ending_movies.py` cuts the twenty ending STRs from the owner's USA disc and packs them like the
opening: 240 pixels wide, Cinepak and IMA ADPCM, sound lined up with the first frame, each stopped at its
descriptor's last frame. In the USA `ending.ovl`, character c uses descriptor 3 + c, and descriptor 3 + c uses
sector row c + 1. The rows are the twenty STRs in disc order, except that rows 18 and 19 belong to characters 18
and 17. The output is game data and stays private:

```sh
python3 scripts/pack_ending_movies.py --libsaturn ../lib-saturn        # build/ending-movies/END00-END18.MOV
cmake -B build -DT3_LOCAL_ENDING_MOVIES=$PWD/build/ending-movies
```

The boot argument that carries the run between the programs is in
`src/saturn/boot_arg.h`: the fight index, a 32-bit seed, the ladder flags, and
the carried score.

## Differences from the original

- The seed is drawn from the frame counter and the free-running timer when the pick is made. The original's generator also advances elsewhere, so the same pick gives a different ladder from the PlayStation's.
- Every fighter counts as unlocked, and there is one costume per character.
- There is no Ogre transformation cutscene; the fight reloads directly against True Ogre.
- The closing screen stands in for the staff roll. The ending movies play without the captions the original draws over four of them (Julia's dialogue, Nina's and Eddy's place captions, Gun Jack's name), and there are no costume endings (Tiger, Panda).
- There is no ranking entry after the run; the game goes back to the title.
- `A NEW CHALLENGER` uses message colour 8; the original's colour 7 is not among the extracted HUD colours.
- CONTINUE? and GAME OVER play no voice or sound.
- A continue retries with the same fighter; the original returns to the select screen first.

## Checks

- `tests/test_arcade_ladder.c` (ctest `arcade_ladder_rules`) checks the ladder's rules for every player over 200 seeds, the first draw, the levels and the countdown digits.
- `-DT3_LADDER_SELFTEST=ON` wins each fight by itself after 240 fields, and at fight 10 it first hands over to True Ogre. `scripts/arcade_ladder_check.py` drives that build on Ymir from the title to the closing screen, keeping a screenshot every 400 fields. On an ordinary build the same script leaves the pad idle after the pick, so the CPU wins and the run shows CONTINUE? and GAME OVER.
