/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of StageAndMusicSelect (0x8004F698) and the True Ogre mask
 * (FUN_800515EC) from pinned Tekken3Remake docs/research/code/stages.md and
 * modes.md. Changes: versus always takes side 1, without the original's
 * tie-break and kept stage, because each fight here is a fresh program. */
#include "stage_rules.h"

enum { TRUE_OGRE=0x14 };

unsigned t3_stage_side(int demo) {return demo?0u:1u;}

int t3_stage_true_ogre(const uint8_t picks[2]) {return picks[0]==TRUE_OGRE || picks[1]==TRUE_OGRE;}
