#!/usr/bin/env python3
"""Deterministic two-pad combat tape (movement, strings, blocks) for matched A/B runs.

Two output forms share one schedule:
- default: a Ymir probe device script, `<field> <port> buttons=<A+B>`;
- --ticks: a simulation-tick fixture for T3_LOCAL_INPUT_TAPE
  (scripts/build_input_tape.py). It drives the simulation boundary directly,
  so two builds whose field timing differs still replay identical inputs on
  identical ticks.
The tape is a pure function of --seed and --length. It carries no game state.
"""
import argparse
import json
from pathlib import Path

# Pad names (device script) and their facing-relative simulation bits.
FORWARD, BACK, LEFT, RIGHT, GUARD, LP, RP, LK, RK, CROUCH, JUMP = (1 << i for i in range(11))
ATTACKS = {'X': LP, 'Y': RP, 'A': LK, 'B': RK, 'X+Y': LP | RP, 'DOWN+A': CROUCH | LK,
           'RIGHT+X': FORWARD | LP, 'LEFT+Y': BACK | RP, 'DOWN+RIGHT+X': CROUCH | FORWARD | LP,
           'A+B': LK | RK}
MOVES = {'RIGHT': FORWARD, 'LEFT': BACK, 'DOWN': RIGHT, 'UP': LEFT,
         'RIGHT+DOWN': FORWARD | RIGHT, 'LEFT+DOWN': BACK | RIGHT}
BUTTONS = LP | RP | LK | RK


def lcg(seed):
    while True:
        seed = (seed * 1664525 + 1013904223) & 0xffffffff
        yield seed >> 8


def schedule(length, seed, start):
    """(time, port, pad name, simulation bits) with one entry per (time, port)."""
    rng = lcg(seed)
    # Port 1 starts on the left: RIGHT closes the gap for it, LEFT for port 2.
    events = [(0, 1, 'NONE', 0), (0, 2, 'NONE', 0),
              (start, 1, 'RIGHT', FORWARD), (start, 2, 'LEFT', FORWARD),
              (start + 80, 1, 'NONE', 0), (start + 80, 2, 'NONE', 0)]
    time = start + 100
    names = list(ATTACKS), list(MOVES)
    while time < length - 40:
        for port in (1, 2):
            roll = next(rng) % 10
            if roll < 5:
                name = names[0][next(rng) % len(ATTACKS)]
                bits = ATTACKS[name]
            elif roll < 8:
                name, bits = ('LEFT' if port == 1 else 'RIGHT'), BACK   # hold back to block
            else:
                name = names[1][next(rng) % len(MOVES)]
                bits = MOVES[name]
            at = time + (port - 1) * 3
            events.append((at, port, name, bits))
            events.append((at + 6 + next(rng) % 12, port, 'NONE', 0))
        time += 24 + next(rng) % 24
    # Strictly one entry per (time, port): a later release at the same time wins.
    seen = {}
    for event in sorted(events, key=lambda e: (e[0], e[1])):
        seen[event[:2]] = event
    return [seen[k] for k in sorted(seen)]


def device_script(events):
    return ''.join(f'{t} {p} buttons={name}\n' for t, p, name, _ in events)


def tick_fixture(events):
    """Rows [tick, p1 held, p1 pressed, p2 held, p2 pressed]; a press lasts one tick."""
    held = {1: 0, 2: 0}
    changes = {}
    for t, p, _, bits in events:
        changes.setdefault(t, []).append((p, bits))
    rows = []
    for t in sorted(changes):
        pressed = {1: 0, 2: 0}
        for p, bits in changes[t]:
            pressed[p] = bits & BUTTONS & ~held[p]
            held[p] = bits
        rows.append([t, held[1], pressed[1], held[2], pressed[2]])
        # Clear the press on the next tick unless a change already lands there.
        if (pressed[1] or pressed[2]) and t + 1 not in changes:
            rows.append([t + 1, held[1], 0, held[2], 0])
    return {'events': rows}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dest', type=Path)
    p.add_argument('--length', '--fields', type=int, default=62000,
                   help='fields (device script) or simulation ticks (--ticks)')
    p.add_argument('--seed', type=int, default=3)
    p.add_argument('--start', type=int, default=600, help='first field/tick after the round intro')
    p.add_argument('--ticks', action='store_true', help='write a T3_LOCAL_INPUT_TAPE JSON fixture')
    a = p.parse_args()
    events = schedule(a.length, a.seed, a.start)
    a.dest.parent.mkdir(parents=True, exist_ok=True)
    a.dest.write_text(json.dumps(tick_fixture(events)) + '\n' if a.ticks else device_script(events))
