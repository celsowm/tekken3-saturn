# Next playable milestone: Paul mirror match on Saturn

Execution evidence and remaining gates: [progress record](NEXT_PLAYABLE_PROGRESS.md).
The current remaining-work sequence is in the [continuation plan](CONTINUATION_PLAN.md).

## Outcome

Deliver a private Saturn build with two original Paul models, authored motion,
responsive player controls, and a small verified set of original moves that
can hit, be guarded, end a round and restart. Demonstrate it through a real
Mednafen CD boot and repeatable headless Ymir scenarios. A full roster, menus
and every Tekken mechanic are outside this milestone.

This is an implementation plan, not a claim that the original combat engine
is already integrated. The private preview now renders textured original Paul
models and authored motion over the provisional match from `src/game/fight.c`.
Owned original-state movement and bounded live combat are now connected in the
separate private `build/paul-combat` configuration; the continuation plan and
progress record identify the remaining replay, round and performance gates.

## Starting evidence

The bullets below describe the initial baseline. For current measurements and
completed research, use the progress record and continuation plan linked above.

- The current application boots from its CUE in Mednafen using `softfb` and
  renders two fighters, an arena and health bars. The saved run has 889 frames.
- The sibling LibSaturn headless Ymir harness builds on ARM64 macOS. Its three
  patches are tracked. Both hello-world and this application execute through
  injection; this path does not certify CD boot.
- Original USA data is fingerprinted and imported privately: 303 BNS records,
  73 DIVMOT records and 104 input sequences. Their structural validation does
  not establish correct motion, input semantics or gameplay parity.
- KMD bind-pose import, optional original-pose skinning, move interpretation,
  collision and post-hit observers exist. They need real-data integration.
- Required USA FK/IK offsets and spline data remain unverified. Original PS1
  capture reached an intro, not FightMain. These are explicit dependencies.
- HUD glyphs appear incomplete in the saved port captures; investigate this
  before relying on on-screen counters.

## 1. Establish executable input and performance regression scenarios

Add a project wrapper around the external Ymir probe, with configurable BIOS,
LibSaturn path, build directory and output directory. Resolve symbols from
the matching ELF/map instead of hardcoding addresses. Use the existing pad,
memory-dump, screenshot and profiling interfaces before extending the probe.

Extend the versioned diagnostics record with sampled controller state and
committed gameplay observations: positions, health, active move, round state
and simulation tick. Decode SH-2 big-endian fields explicitly. Frame serials,
emulator frames and simulation ticks must remain separate time domains.

Create input schedules for neutral, approach/retreat, all four attack buttons,
guard, crouch, sidestep, release/repress, reset and a connected second pad.
Use a stationary controlled opponent where needed so CPU behavior cannot
explain the result. Sample at defined checkpoints or equivalent bounded runs.

Acceptance: scripted controls cause the expected movement and state changes;
holding a button does not manufacture repeated presses; connected P2 replaces
CPU input; reset restores the match. Fix HUD text and confirm it visually.
Record command capacity/use, rejected faces, draw timeouts, simulation progress
and visible rendering at baseline. Repeated runs must reproduce gameplay
observations. Host tests alone cannot satisfy this step.

## 2. Render original Paul geometry and motion

Identify and document the actual Paul KMD and associated DIVMOT records using
the private inventory and pinned Tekken3Remake research. Do not infer identity
only from a record number. Start with the existing `T3_LOCAL_KMD` path in a
separate private build directory, preserving the procedural baseline.

Locate the five USA FK/IK tables and spline coefficients in the fingerprinted
EXE. Verify code references, table structure and representative decoded poses;
the extractor currently requires offsets and does not discover them. Feed
verified data into the existing motion decoder and C/Python differential audit.
Check all required joint/part masks, scale, axes, handedness and root placement.

Render idle, walk and one authored attack first, then two independently posed
instances. Use flat materials initially. Add the minimum TIM/palette/UV
conversion needed for recognizable Paul textures after geometry and poses work;
document texture dimensions, residency, palette allocation and both actors'
ownership. Never silently substitute a bind pose for an unavailable animation.

Acceptance: captures show recognizable Paul geometry and continuous authored
motion, with intact limbs and independently controlled poses. Selected real
streams pass numerical audits with coverage and unavailable data reported.
Produce a static model checkpoint even if table verification needs more work;
do not label that checkpoint an animated playable port.

## 3. Obtain a small original-game reference set

Use the sibling PCSX-Redux interpreter baseline and fix controller automation
until the unmodified original reaches FightMain. Diagnose Lua loading, callback
execution, port configuration and button polarity separately. Avoid making the
ARM64 dynarec repair a prerequisite when interpreter execution suffices.

Capture Paul mirror-match clips for idle, walk, one high, one mid, one low,
guard and a round-ending hit. Identify actual move slots and supported commands
from observations rather than assigning the prototype's attacks to new names.
Retain input schedule, executable hash, starting state and source-frame timing.
Use the existing Lua recorder and independent RAM decoder to cross-check a
paused observation. Establish where capture occurs relative to move and hit
processing before comparing tick numbers.

Acceptance: consecutive valid FightMain observations include real move/pose
transitions and measured health changes. Publish only code and methodology;
keep game data, savestates and captures private. If this remains blocked,
continue model, texture and Saturn-input work, but record combat parity as
unavailable and do not invent original timings or damage.

## 4. Activate a bounded original-combat path

Introduce an explicit experimental original-combat mode with a single owner
of authoritative match state. Connect input history to verified DIVMOT move
transitions, authored pose/root motion, world-space hit/hurt geometry and
verified damage/reactions. Simulation and rendering must consume the same move
and pose state. The current probe must not remain an unrelated second fight.

Support only the reference-set moves initially: neutral/walk, selected high,
mid and low, appropriate standing/crouching defense, hit reaction and round
completion. Account for attack continuity, cooldowns and simultaneous contact.
Audit every required command/condition, shared-bank transition, reaction table
and movement rule before enabling that path. List unsupported moves explicitly.
Keep the provisional mode available for regression comparison; do not mix its
damage constants or invented reaction durations into the original mode.

Acceptance: replay matched inputs through host and Saturn implementations,
compare move/pose, health and reaction observations against the aligned PS1
clips, and resolve differences for the supported subset. Enable live damage
for that subset once its observations agree; full-roster parity is not required.
Demonstrate a controlled hit, a correctly guarded attack and round completion
under scripted input. Report any retained prototype round policy explicitly.

## 5. Validate the complete slice and fit the Saturn budget

Boot the private CD in Mednafen and record the supported encounter from idle
through movement, attacks, defense and round end. Repeat behavioral cases in
headless Ymir, including reset and two-pad mode. Attach before/after captures,
machine-readable observations and exact build/emulator/data revisions.

Measure resident RAM, asset bytes, VDP1 texture memory, command peaks, skinning
cost and actual render cadence separately from the fixed 60 Hz logic. Choose
a 30 or 60 Hz rendering target from measured results. Optimize the measured
bottleneck before adding Slave SH-2 work, streaming or requiring a RAM cart.
Generic library corrections belong in sibling `~/dev/lib-saturn`, with relevant
example/harness verification there and a rebuilt installed package here.

Acceptance: both actors animate, input scenarios pass, the supported original
combat subset completes a round, and actual CD boot remains confirmed. No
runtime memory or command overflow and no draw timeouts in the bounded suite.
Report measured cadence and remaining mismatches; emulator observations do not
claim hardware performance.

## Execution order and completion boundary

Execute step 1 first, then the static geometry checkpoint in step 2. Establish
the original reference set in step 3 before committing to gameplay semantics;
finish authored motion/textures and then step 4. Finish with step 5 after every
integration change. If one research dependency blocks, advance independent
deliverables and keep that dependency visible rather than accumulating more
unconnected observer modules.

The milestone is complete only with a demonstrated supported Paul encounter,
not with compilation or static screenshots alone. Subsequent work can expand
Paul's moves, throws/juggles/get-ups, other fighters, stages and audio. Character
selection and additional modes follow a credible playable fight.

Tekken3Remake remains the pinned implementation/research reference. Respect its
license and notices for any adapted code. PCSX-Redux supplies original-game
observations; Ymir remains an external test executable rather than a linked
game dependency. Retail assets and BIOS stay outside Git.
