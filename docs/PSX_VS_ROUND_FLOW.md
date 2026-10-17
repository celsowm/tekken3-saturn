# Original normal-VS round-flow contract

This is the integration contract for `src/game/psx_round_flow.c`, adapted from
the pinned Tekken3Remake `round_flow.gd`. It is a portable control component;
the playable application still uses `psx_round.c`'s bounded round wrapper.
Component agreement does not establish uninterrupted match or Saturn parity.

## Ownership and call order

The original FightFrame first processes a pending replay request, calls
ReplayUpdate, then RoundFlow. Only afterward does it advance the live fighters
or draw the replay frame. RoundFlow can therefore start a victory move at pose
1 and MoveStartAll can advance it to pose 2 within the same FightFrame.
Sampling at GPU Vsync cannot substitute for these instruction boundaries.

The component owns the observed round control fields, VS scoring, normalized
and carried health, scripted stance input and active/win-pose markers. Its
explicit actions request move starts, grounded placement, blend clearing,
KO-side animation and one cumulative fight-clock increment. The match caller
must apply those actions in original order. It must separately own actual
pose/ground evaluation, camera advancement, replay storage/playback and the
outer FightMain transition to a new round. Sound/effect events are not ported
by this component. Cumulative fight-clock storage is not in the current audit.

The original cumulative frame counter supplies victory parity, and the physical
pad word supplies face-button overrides. Neither is the round timer or the
number of native simulation steps. The current continuous replay adapter must
be extended to provide these facts and seed round state once before integration.

## Transitions

| Entry | Condition and writes | Exit |
| --- | --- | --- |
| INTRO (0) | Increment round frame; leave timer unchanged. Enable both input sources at frame 151 for the first round, 91 thereafter. | FIGHT (1) at threshold |
| FIGHT (1) | Increment round frame. Check zero HP/time before decrementing timer. When not over, decrement only if timer is running; cumulative fight clock advances even if timer is stopped. | FIGHT or RESULT (2) |
| RESULT (2) entry | Normalize HP with integer ceiling, classify result, award scores, copy carried HP and reset pose-completion flags. Hold is 90 for timeout, 240 otherwise; done becomes 0. Timeout disables both input sources immediately. | RESULT |
| RESULT (2) steps | Increment round counter. Loser/zero-HP actor uses script 0; a human KO winner keeps physical input; otherwise preserve the winner's stance through script input. At counter 90, time/perfect-result bits mark done. Hold is not decremented here. | REPLAY_DONE (4), request replay, input sources 0 |
| REPLAY_DONE (4) | Wait until ReplayUpdate reports mode 5. A nonterminal skipped result may end directly. Otherwise choose victory pose, configure camera/active actor, clear blends and fall through victory initialization. | WIN_WAIT (6) |
| WIN_POSES (5) | Set hold 300, zero counter/done/completion flags, fall through the first wait step. | WIN_WAIT (6), hold 299 |
| WIN_WAIT (6) | Increment counter, set blend hold 2, decrement hold. Camera 2 completes at counter 75; loser cameras at 60; winner camera 5 waits until both pose-completion flags are set. Hold exhaustion, permitted skip or human Start also end the round. | END (9) |
| END (9) | RoundFlow leaves next-round initialization to FightMain. A terminal match remains terminal. | External transition |

First-round intro, physical button priority, Start/skip, stopped timer and CPU
loser-camera branches have synthetic/reference tests; not all have current
original-emulator branch coverage. Normal two-human Paul VS is the observed
integration target. Team, survival, arcade Ogre transitions and special costume
0x26 are outside this component's supported contract.

## Captured transition examples

All source counters below are original main-loop observations, not native tick
numbers. `ps1-paul-flow-complete/` independently captures a P1 timeout followed
by a perfect KO. Each final paused RAM field agrees with the Lua observation.

| Source counter | Original edge | Relevant result |
| --- | --- | --- |
| 2704 | 1 → 2 | Timeout, P1 score 1–0, hold 90 |
| 2794 | 2 → 4 | Counter 90, hold still 90, replay requested |
| 2795 | 4 → 6 | Replay already done for timeout; P1 slot 3432 at pose 1; P2 inactive; hold 299 |
| 2911 | 6 → 9 | Victory completion, counter 116, hold 183 |
| 2915 | First next-round call, entry 0 | Round frame 0 → 1, timer 2400, restored HP; P1 slot 1 and P2 slot 3 |
| 3005 | 0 → 1 | Later intro frame 91; timer still 2400 |
| 5008 | 1 → 2 | Perfect KO, match 2–0, hold 240 |
| 5098 | 2 → 4 | Counter 90, hold still 240 |
| 5099–5398 | 4 → 4 | 300 RoundFlow calls while original replay is not done |
| 5399 | 4 → 6 | Replay done; P1 slot 3432 starts |
| 5515 | 6 → 9 | Victory completes; terminal match |

`ps1-paul-flow-draw/` captures both neutral draws through their final END states.
Scores are 1–1 then 2–2; max rounds decreases 3 → 2 → 1. Neither actor starts
a victory move, both stay active, camera remains 2, and each WIN_WAIT ends at
counter 75. The first next-round intro reaches frame 91 before enabling input.

`ps1-paul-flow-p2-stance/` adds independently checked P2 timeout/KO victories,
ending the match 0–2. Its victory starts choose P2 slots 3432 and 3431 at
source counters 2717 and 5288, exercising parity and different move lengths.
The three passing captures total 16,334 paired component calls with no checked
differences; they do not extend the one-seed fighter replay's matching prefix.

Next-round setup changes persistent fighter records selectively in the original.
The bounded wrapper's full zero/reset to slot 3 for both actors is not evidence
for that setup and must not be reused as an original reset implementation.

## Read-only reproduction and audit

```sh
python3 scripts/prepare_psx_phase_capture.py \
  --exe assets/imported/tekken3-usa/SLUS_004.02 \
  --output assets/imported/local-flow-capture \
  --first 550 --last 6500 --round-flow-ram --stop-round-ends 2
cmake --build build/host-public --target t3_round_flow_eval
python3 scripts/audit_psx_round_flow.py \
  --capture assets/imported/local-flow-capture \
  --runner build/host-public/t3_round_flow_eval \
  --output assets/imported/local-flow-capture/native-flow-audit.json
```

Run the prepared launcher after the archived Paul session script in PCSX-Redux's
interpreter/debugger, as in the existing capture guide. Session options
`{neutral=true}` hold both pads neutral after UI navigation;
`{attacker=2}` mirrors the existing attack/defense schedule onto P2. Archive the
observer/session, final execution state, latest phase, disc identity and full
paused RAM before stopping the owned process. The audit requires those paused
observations, verified descriptors and complete call nesting. Do not ask the
user to operate the emulator for this workflow.

The audit checks every observed control field and the declared actor outputs
against independent compact-RAM reads, then runs the same portable C component
for every entry/return pair. It records trace, adapter, paused RAM and snapshot
hashes and retains all discrepancies. Each call is independently seeded;
continuous fighter, replay and rendering parity remain separate gates.

USA PadRead stores physical words as **two adjacent unsigned halfwords** at
`0x800ADD68/+2`, and pressed words at `0x800ADF08/+2`. USA instructions
`0x80029F88/8C` and StartPressed `0x8002BA18/1C` establish those widths and
strides. The preparer records them explicitly rather than mapping a second
32-bit array element from isolated layout points.

When HoldInput has no pending move, it looks up the current pose's stance slot.
USA FighterMoveForSlot `0x8002D178` follows the bank table at `0x800ADC20`,
fighter `player_index * 4`, bank `+0x0C`, then stance slot `* 4`. New flow
captures include that complete pointer chain and stance row when needed. This
requires up to 64 bounded compact ranges; both host readers accept that limit.
Missing stance evidence is an audit error, not an assumption of standing input.
The earlier `ps1-paul-flow-p2/` capture lacks this chain and cannot support the
complete P2 flow audit. Keep it as incomplete evidence.
