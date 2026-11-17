#!/usr/bin/env python3
"""Export golden cases of the Tekken Ball ball for tests/test_ball_rules.c.

Runs the pinned Tekken3Remake's verified port (`ball_sim.ball_update`, `ball_after_update`, `move_tuning`
and `fight_start`; tools/research/verify_ball_sim.py shows it equal to the original code) on random fight
states, one step per case, and writes each case's input and expected output as the flat int32 vector of
src/game/ball_rules.h (the ball, the engine words, the two fighters' facts) plus the events the step
requested. The engine calls (sound, vibration, shake, replay, popup, burn, glow) are logged instead of run.
Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_cases.py \
        --out build/ball-cases/rules.bin --cases 4000

The file embeds the sine and atan tables of the executable, so it is retail-derived and stays under build/.
Layout (big-endian int32): 'T3BC', version, vector length, case count, table sizes; the trig and atan
tables; one byte per vector field (1: the C module's own, not compared); then per case: mode, s7, the 12
option row words, vector before, vector after, event count, 16 events of 6 words.
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
sys.path.insert(0, str(RESEARCH))

import ball_sim as bs            # noqa: E402
import draw_sim as ds            # noqa: E402
import projection_sim as pj      # noqa: E402
from fight_sim import Ram       # noqa: E402

MAGIC = 0x54334243               # 'T3BC'
VERSION = 1
EVENTS = 16
F0, F1 = 0x800A96F0, 0x800AAF7C
FIGHTERS = (F0, F1)
BALL = 0x80180000
POSITIONS = 0x801FE000
MOVES = (0x801E0000, 0x801E0200)
PROJ_BASE = 0x801D0000
EXE_BASE = 0x80010000
TRIG_BYTES, ATAN_BYTES = 10240, 1025
ATAN_TABLE = 0x800210C8
OPTION_ROW = bs.BALL_TABLE

SOUND, VIBRATE, SHAKE, REPLAY, POPUP, BURN, STAND_IN, GLOW = range(1, 9)
M_UPDATE, M_AFTER, M_TUNING, M_START = range(4)

fighter_addr = {0: F0, 1: F1}


class Field:
    """One int32 of the vector: where it lives in the RAM and how it is read and written."""

    def __init__(self, name, addr, fmt, skip=False, reader=None):
        self.name, self.addr, self.fmt, self.skip, self.reader = name, addr, fmt, skip, reader

    def read(self, ram):
        if self.reader:
            return self.reader(ram)
        return ram.get(self.addr, self.fmt)

    def write(self, ram, v):
        ram.put(self.addr, self.fmt, v)


class Index(Field):
    """A fighter pointer kept as the fighter's index."""

    def read(self, ram):
        return (ram.u32(self.addr) - F0) // 0x188C

    def write(self, ram, v):
        ram.put(self.addr, 'I', F0 + 0x188C * v)


def ball_fields():
    b = BALL
    out = [Field('state', b, 'h')]
    out += [Field(f'pos{i}', b + 0x68 + 4 * i, 'i') for i in range(3)]
    out += [Field(f'vel{i}', b + 0x78 + 4 * i, 'i') for i in range(3)]
    out += [Field('gravity', b + 0x8C, 'i')]
    out += [Field(f'spin{i}', b + 0x98 + 2 * i, 'h') for i in range(3)]
    out += [Field(f'spin_rate{i}', b + 0xA0 + 2 * i, 'h') for i in range(3)]
    out += [Field('speed', b + 0xA8, 'i'), Field('sweep', b + 0xAC, 'i'),
            Field('angle', b + 0xB0, 'h'), Field('homing', b + 0xB2, 'h')]
    out += [Field(f'prev{i}', b + 0xB8 + 4 * i, 'i') for i in range(3)]
    out += [Field('touch_wait', b + 0xCC, 'B'), Field('squash_hold', b + 0xCD, 'B')]
    out += [Field(f'volume{j}_{i}', b + 0xD0 + 0x18 * j + 4 * i, 'i') for j in range(4) for i in range(6)]
    out += [Index('owner', b + 0x130, 'I'), Index('target', b + 0x134, 'I'), Index('spike_target', b + 0x138, 'I')]
    out += [Field(f'power{i}', b + 0x13C + 2 * i, 'h') for i in range(2)]
    out += [Field('charge', b + 0x140, 'h'), Field('spike_speed', b + 0x142, 'h'),
            Field('rehit_count', b + 0x144, 'B'), Field('rehit', b + 0x145, 'B'),
            Field('kind', b + 0x146, 'B'), Field('timer', b + 0x147, 'B')]
    out += [Field(f'squash_axis{i}', b + 0x148 + 2 * i, 'h') for i in range(3)]
    out += [Field('squash', b + 0x150, 'h')]
    return out


T = bs.THIRD


def world_fields():
    out = [Field('speed_unit', bs.SPEED_UNIT, 'h'), Field('spike_unit', bs.SPIKE_UNIT, 'h'),
           Field('gravity_unit', bs.GRAVITY, 'h'), Field('neutral', bs.NEUTRAL, 'h'), Field('gain', bs.GAIN, 'h'),
           Field('idle', bs.IDLE, 'i'), Field('touch_sound_wait', bs.TOUCH_SOUND_WAIT, 'i'),
           Field('point', bs.POINT, 'i'), Field('charged_hit', bs.CHARGED_HIT, 'B'),
           Field('result', bs.RESULT, 'i'), Field('no_score', bs.NO_SCORE, 'i'),
           Field('no_winner_mark', bs.NO_WINNER_MARK, 'i'), Field('round_clock', bs.ROUND_CLOCK, 'i'),
           Field('lob_base', bs.LOB_BASE, 'i'), Field('round_state', 0x80097350, 'i'),
           Field('option', 0x800AFF91, 'B'), Field('first_round', 0x800958A8, 'i'),
           Field('serve_side', bs.MODE_FLAGS, 'B'), Field('glows', bs.GLOWS, 'B')]
    out += [Field('stand_in.source', T + 0x400, 'i', skip=True),       # not a word of the original
            Field('stand_in.damage', T + 0x5E, 'h'), Field('stand_in.level', T + 0x64, 'H'),
            Field('stand_in.hit83_0', T + 0x83, 'B'), Field('stand_in.hit83_1', T + 0x84, 'B'),
            Field('stand_in.hit88', T + 0x88, 'B'), Field('stand_in.hitD1', T + 0xD1, 'B'),
            Field('stand_in.hit23', T + 0x23, 'B'), Field('stand_in.move_byte', T + 0x404, 'i', reader=lambda r: r.u8(r.u32(T + 0x54) + 0x2C)),
            Field('stand_in.x', T + 0xF68, 'i'), Field('stand_in.z', T + 0xF70, 'i')]
    out += [Field(f'hit_offsets{j}_{i}', bs.HIT_VOLUME + 8 * j + 2 * i, 'h') for j in range(4) for i in range(3)]
    return out


def fighter_fields(n):
    f = FIGHTERS[n]
    mv = MOVES[n]
    out = [Field('index', f + 0x1E, 'B'), Field('player', f + 0x12, 'h'), Field('x', f + 0xF68, 'i'),
           Field('z', f + 0xF70, 'i'), Field('head', f + 0x950, 'i'), Field('health', f + 0x3F4, 'i'),
           Field('kind', f + 0x16, 'h'), Field('level', f + 0x64, 'H'), Field('damage', f + 0x5E, 'h'),
           Field('guard', f + 0x66, 'H'), Field('slot', f + 0xA0, 'h'), Field('locked', f + 0x85, 'B'),
           Field('f88', f + 0x88, 'B'), Field('fd1', f + 0xD1, 'B'), Field('f23', f + 0x23, 'B'),
           Field('hit83_0', f + 0x83, 'B'), Field('hit83_1', f + 0x84, 'B'),
           Field('move_byte', mv + 0x2C, 'B'), Field('airborne', f + 0xDB, 'B'),
           Field('busy3', f + 0xC3, 'B'), Field('busy4', f + 0xC4, 'B'),
           Field('projectile', f + 0x400, 'i', skip=True), Field('seg_count', f + 0xE3, 'B')]
    out += [Field(f'seg{k}_{i}', f + 0x1AC + 0x18 * k + 4 * i, 'i') for k in range(4) for i in range(6)]
    out += [Field(f'cyl{k}_{i}', f + 0x20C + 0x14 * k + 4 * i, 'h' if i == 3 else 'i') for k in range(14) for i in range(5)]
    out += [Field('opponent_flag', f + 0x22, 'B'), Field('contact_cyl', f + 0x154, 'H')]
    out += [Field(f'contact_end{i}', f + 0x13C + 4 * i, 'i') for i in range(3)]
    out += [Field(f'contact_dir{i}', f + 0x14C + 2 * i, 'h') for i in range(3)]
    out += [Field('f87', f + 0x87, 'B'), Field('fce', f + 0xCE, 'B'), Field('fee', f + 0xEE, 'H'),
            Field('f15d', f + 0x15D, 'B'), Field('f15c', f + 0x15C, 'B'), Field('f104', f + 0x104, 'H')]
    d = bs.DRIFT + 6 * n
    out += [Field('drift0', d, 'h')] + [Field(f'drift{i}', d + 1 + i, 'B') for i in range(1, 5)]
    return out


FIELDS = ball_fields() + world_fields() + fighter_fields(0) + fighter_fields(1)


def vector(ram, projectile):
    out = []
    for fl in FIELDS:
        if fl.name == 'stand_in.source':
            out.append(-1)
        elif fl.name == 'projectile':
            out.append(projectile[0] if fl.addr == F0 + 0x400 else projectile[1])
        else:
            out.append(fl.read(ram))
    return out


class Log:
    """Logs the engine calls as event vectors."""

    def __init__(self, ram):
        self.ram, self.events = ram, []
        self.last_text = 0

    def add(self, kind, a=0, b=0, c=0, d=0, e=0):
        self.events.append([kind, a, b, c, d, e])

    def sound(self, ram, sid): self.add(SOUND, sid)
    def vibrate(self, ram, player, kind): self.add(VIBRATE, player, kind)
    def camera_shake(self, ram, kind): self.add(SHAKE, kind)
    def replay_event(self, ram, player, kind): self.add(REPLAY, player, kind)


ORIGINAL_GLOW = bs.glow


def install(log):
    bs.LOCALS = 0x801FD000                 # the game's scratchpad locals, in RAM here
    bs.PHYSICS_HOOKS = log
    bs.damage_text = lambda ram, n, out: setattr(log, 'last_text', n)

    def popup(ram, gte, text, pos, colour, a, b):
        number = -1 if text == bs.EXCLAIM else log.last_text
        log.add(POPUP, number, colour, ram.s32(pos), ram.s32(pos + 4), ram.s32(pos + 8))
    ds.ball_popup_add = popup
    bs.burn = lambda ram, player: log.add(BURN, player)
    ds.effect_alloc = lambda ram: 0x1000
    orig = ORIGINAL_GLOW

    def glow(ram, player, big):
        u = player + 2 * big
        if not ram.u8(bs.GLOWS) & (1 << (u & 31)):
            log.add(GLOW, player, big)
        orig(ram, player, big)
    bs.glow = glow


def s16(v): return (v + 0x8000 & 0xFFFF) - 0x8000


def new_ram(exe):
    ram = Ram(bytes(0x200000))
    off = ram._off(EXE_BASE)
    ram.data[off:off + len(exe)] = exe
    return ram


def randomise(ram, rnd):
    """A random fight state around the ball: the fighters, the ball, the engine words."""
    def r(a, b): return rnd.randint(a, b)
    b = BALL
    state = rnd.choice([0, 1, 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 2, 3, 2])
    kind = r(0, 2)
    row = bs.BALL_TABLE + 24 * kind
    unit = (384, 368, 352)[kind]
    ram.put(0x800AFF91, 'B', kind)
    for dst, v in ((bs.SPIKE_UNIT, unit), (bs.SPEED_UNIT, r(300, 400)), (bs.GRAVITY, (512, 512, 682)[kind]),
                   (bs.GAIN, (60, 80, 100)[kind]), (bs.NEUTRAL, (2304, 1920, 1536)[kind])):
        ram.put(dst, 'H', v)
    ram.put(bs.IDLE, 'I', rnd.choice([0, 0, r(0, 700)]))
    ram.put(bs.TOUCH_SOUND_WAIT, 'I', rnd.choice([0, 0, r(1, 10)]))
    ram.put(bs.NO_SCORE, 'I', rnd.choice([0, 0, 0, 1]))
    ram.put(bs.NO_WINNER_MARK, 'I', rnd.choice([0, 0, 0, 1]))
    ram.put(bs.ROUND_CLOCK, 'I', r(0, 700))
    ram.put(bs.LOB_BASE, 'I', rnd.choice([r(0x400, 0x6000), r(0, 0x500), r(0x5000, 0x9000)]))
    ram.put(0x80097350, 'I', rnd.choice([0, 1, 3, 6, 7]))
    ram.put(bs.RESULT, 'I', r(0, 2))
    ram.put(bs.GLOWS, 'B', r(0, 15))
    ram.put(bs.MODE_FLAGS, 'B', r(0, 1))
    ram.put(0x800958A8, 'I', r(0, 2))
    for j in range(4):
        for i in range(3):
            ram.put(bs.HIT_VOLUME + 8 * j + 2 * i, 'H', 0 if j == 0 else rnd.choice([0x80, -0x80]))
    # the ball
    x = rnd.choice([r(-0x2000, 0x2000), r(-0x300, 0x300), r(-0x3000, 0x3000)])
    y = rnd.choice([r(-0x1400, -0x180), r(-0x700, -0x170), r(-0x2200, -0x1300), -0x180, r(-0x190, -0x170)])
    z = r(-0x100, 0x100)
    ram.put(bs.BALL_PTR, 'I', b)
    ram.put(b, 'H', state)
    for i, v in enumerate((x, y, z)):
        ram.put(b + 0x68 + 4 * i, 'I', v)
    for i in range(3):
        ram.put(b + 0x78 + 4 * i, 'I', r(-0x4000, 0x4000) if i != 1 else rnd.choice([r(-0x4000, 0x4000), r(0, 0x4000)]))
    ram.put(b + 0x8C, 'I', rnd.choice([0, 512, 682, r(-600, 700)]))
    for i in range(3):
        ram.put(b + 0x98 + 2 * i, 'H', r(0, 0xFFFF))
        ram.put(b + 0xA0 + 2 * i, 'H', r(0, 0xFFFF) if rnd.random() < .3 else r(0, 40))
    ram.put(b + 0xA8, 'I', rnd.choice([0, r(0, 0x6000), r(0x800, 0x20000)]))
    ram.put(b + 0xAC, 'I', r(0, 5))
    ram.put(b + 0xB0, 'H', rnd.choice([r(0, 0xFFF), r(-0x1000, 0x1000) & 0xFFFF]))
    ram.put(b + 0xB2, 'H', rnd.choice([0, 0, 1]))
    for i in range(3):
        ram.put(b + 0xB8 + 4 * i, 'I', r(-0x2000, 0x2000))
    ram.put(b + 0xC4, 'H', bs.ds._s16(0x260) & 0xFFFF)
    ram.put(b + 0xC8, 'I', 0x5A400)
    ram.put(b + 0xCC, 'B', rnd.choice([0, 0, 0, r(1, 5)]))
    ram.put(b + 0xCD, 'B', r(0, 1))
    for j in range(4):
        for i in range(6):
            ram.put(b + 0xD0 + 0x18 * j + 4 * i, 'I', r(-0x2000, 0x2000))
    ram.put(b + 0x130, 'I', FIGHTERS[r(0, 1)])
    ram.put(b + 0x134, 'I', FIGHTERS[r(0, 1)])
    ram.put(b + 0x138, 'I', FIGHTERS[r(0, 1)])
    ram.put(b + 0x13C, 'H', rnd.choice([0, r(0, 60), r(0, 200)]))
    ram.put(b + 0x13E, 'H', rnd.choice([0, r(0, 60), r(0, 200)]))
    ram.put(b + 0x140, 'H', rnd.choice([0, 0, r(1, 99), r(100, 300)]))
    ram.put(b + 0x142, 'H', r(0, 6000))
    ram.put(b + 0x144, 'B', r(0, 5))
    ram.put(b + 0x145, 'B', r(0, 1))
    ram.put(b + 0x146, 'B', r(0, 10))
    ram.put(b + 0x147, 'B', rnd.choice([1, 1, 2, r(0, 255)]))
    for i in range(3):
        ram.put(b + 0x148 + 2 * i, 'H', r(0, 0xFFFF))
    ram.put(b + 0x150, 'H', rnd.choice([r(0, 0x300), r(0, 0xFFFF)]))
    # the stand-in attacker
    ram.put(T + 0x5E, 'H', r(0, 300))
    ram.put(T + 0x64, 'H', rnd.choice([0xFFFF, r(0, 0xFFFF), 0x412]))
    ram.put(T + 0x54, 'I', MOVES[1])
    ram.put(MOVES[1] + 0x2C, 'B', r(0, 255))
    for a in (0x83, 0x84, 0x88, 0xD1, 0x23):
        ram.put(T + a, 'B', r(0, 2))
    ram.put(T + 0xF68, 'I', r(-0x2000, 0x2000))
    ram.put(T + 0xF70, 'I', r(-0x100, 0x100))
    # the fighters
    ram.put(POSITIONS, 'I', F0 + 0xF68)
    ram.put(POSITIONS + 4, 'I', F1 + 0xF68)
    proj = [0, 0]
    body = rnd.random() < 0.25            # a charged ball in the air touches a body, no attack segment
    if body:
        ram.put(b, 'H', rnd.choice([2, 3]))
        ram.put(b + 0xCC, 'B', 0)
        ram.put(b + 0x6C, 'I', r(-0x1000, -0x300))
        ram.put(b + 0x140, 'H', r(90, 300))
        ram.put(b + 0x130, 'I', FIGHTERS[r(0, 1)])
        ram.put(b + 0x146, 'B', rnd.choice([6, 7, 9, r(0, 10)]))
    for n in range(2):
        f, mv = FIGHTERS[n], MOVES[n]
        fx = rnd.choice([r(-0x1400, 0x1400), x + r(-0x400, 0x400)])
        fz = r(-0x100, 0x100)
        ram.put(f + 0x1E, 'B', n)
        ram.put(f + 0x12, 'H', n)
        ram.put(f + 0xF68, 'I', fx)
        ram.put(f + 0xF70, 'I', fz)
        ram.put(f + 0x950, 'I', r(-0x900, -0x300))
        ram.put(f + 0x3F4, 'I', rnd.choice([r(0, 0x640000), r(0x1E0000, 0x640000), r(0, 0x400000)]))
        ram.put(f + 0x16, 'H', rnd.choice([4, r(0, 20), r(0, 20)]))
        ram.put(f + 0x64, 'H', rnd.choice([0x412, 0x217, 0x31F, 0x10F, 0x51F, 0x607, 0x706, r(0, 0x7FF)]))
        ram.put(f + 0x5E, 'H', rnd.choice([0, 0, r(1, 40), r(0, 300)]))
        ram.put(f + 0x66, 'H', rnd.choice([0, 0x412, r(0, 0xFFFF)]))
        ram.put(f + 0xA0, 'H', rnd.choice([r(0x18, 0x22), r(0, 0x200), 0x181]))
        ram.put(f + 0x85, 'B', rnd.choice([0, 0, 0, 1]))
        for a in (0x88, 0xD1, 0x23, 0x83, 0x84, 0x22, 0x87, 0xCE, 0x15D, 0x15C):
            ram.put(f + a, 'B', r(0, 2))
        ram.put(f + 0xDB, 'B', rnd.choice([0, 0, 1]))
        ram.put(f + 0xC3, 'B', rnd.choice([0, 1]))
        ram.put(f + 0xC4, 'B', rnd.choice([0, 0, 0, 1]))
        ram.put(f + 0xEE, 'H', r(0, 255))
        ram.put(f + 0x104, 'H', r(0, 5))
        ram.put(f + 0x154, 'H', r(0, 13))
        for i in range(3):
            ram.put(f + 0x13C + 4 * i, 'I', r(-0x2000, 0x2000))
            ram.put(f + 0x14C + 2 * i, 'H', r(0, 0xFFFF))
        ram.put(f + 0x54, 'I', mv)
        proj[n] = 0 if body else rnd.choice([0, 0, 0, 1])
        ram.put(mv + 0x28, 'I', mv + 0x40)                  # a pointer to the move's kind byte
        ram.put(mv + 0x40, 'B', 0x18 if proj[n] else 0x10)
        ram.put(mv + 0x2C, 'B', r(0, 255))
        drift = bs.DRIFT + 6 * n
        ram.put(drift, 'H', r(0, 0xFFFF))
        for i in range(2, 6):
            ram.put(drift + i, 'B', r(0, 255))
        # attack segments, around the ball most of the time
        count = 0 if body else r(0, 4)
        ram.put(f + 0xE3, 'B', count)
        near = body or rnd.random() < .6
        bx, by, bz = (ram.s32(b + 0x68 + 4 * i) for i in range(3))

        def seg():
            if near:
                c = (bx + r(-300, 300), by + r(-300, 300), bz + r(-300, 300))
                d = (r(-500, 500), r(-500, 500), r(-500, 500))
                return [c[0] - d[0], c[1] - d[1], c[2] - d[2], c[0] + d[0], c[1] + d[1], c[2] + d[2]]
            return [r(-0x2000, 0x2000) for _ in range(6)]
        for k in range(4):
            for i, v in enumerate(seg()):
                ram.put(f + 0x1AC + 0x18 * k + 4 * i, 'I', v)
        # the projectile segments (the idx <= 2 table)
        ram.put(0x800AE148, 'I', PROJ_BASE)
        ram.put(0x800AE374 + 4 * n, 'I', count)
        for k in range(4):
            for i, v in enumerate(seg()):
                ram.put(PROJ_BASE + n * 0x780 + 0x3200 + 0x18 * k + 4 * i, 'I', v)
        # hurt cylinders around the ball half of the time
        for k in range(14):
            c = f + 0x20C + 0x14 * k
            rad = rnd.choice([0, r(60, 400), r(60, 400)])
            if near or rnd.random() < .3:
                centre = (bx + r(-500, 500), by + r(-500, 500), bz + r(-500, 500))
            else:
                centre = (r(-0x2000, 0x2000), r(-0x1500, 0), r(-0x200, 0x200))
            for i in range(3):
                ram.put(c + 4 * i, 'I', centre[i])
            ram.put(c + 0xC, 'I', rad)
            ram.put(c + 0x10, 'I', rad * rad)
    return proj


def effective_segments(ram, proj):
    """The attack segments the original tests: its own, or the projectile table's (index <= 2)."""
    for n in range(2):
        f = FIGHTERS[n]
        if proj[n]:
            count = ram.s32(0x800AE374 + 4 * n)
            src = PROJ_BASE + n * 0x780 + 0x3200
            ram.put(f + 0xE3, 'B', min(max(count, 0), 255))
            for i in range(24):
                ram.put(f + 0x1AC + 4 * i, 'I', ram.u32(src + 4 * i))


def make_case(exe, rnd, mode):
    ram = new_ram(exe)
    proj = randomise(ram, rnd)
    log = Log(ram)
    install(log)
    s7 = rnd.choice([0, 0, 1, 2, 3])
    row = [0] * 12
    if mode == M_START:
        row = [rnd.randint(0, 800) for _ in range(12)]
        for i, v in enumerate(row):
            ram.put(bs.BALL_TABLE + 24 * ram.u8(0x800AFF91) + 2 * i, 'H', v)
    # the original's projectile source: the segments copied in the record as seen by the test
    snapshot = ram.data[:]
    effective = Ram(snapshot)
    effective_segments(effective, proj)
    before = vector(effective, proj)
    fa, fb = FIGHTERS
    if mode == M_UPDATE:
        bs.ball_update(ram, None, BALL, fa, fb, POSITIONS, s7)
        # the original resolves a projectile move's segments itself
    elif mode == M_AFTER:
        bs.ball_after_update(ram, BALL)
    elif mode == M_TUNING:
        n = s7 = rnd.randint(0, 1)                 # which fighter's move slot is tuned
        bs.move_tuning(ram, FIGHTERS[n])
    else:
        try:
            bs.fight_start(ram)
        except Exception:
            return None
    final = Ram(ram.data[:])
    effective_segments(final, proj)
    after = vector(final, proj)
    return mode, s7, row, before, after, log.events


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=4000)
    parser.add_argument('--seed', type=int, default=7)
    args = parser.parse_args()
    exe = (ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin').read_bytes()[0x800:]
    rnd = random.Random(args.seed)
    trig = exe[bs.pj.SIN_TABLE - EXE_BASE:bs.pj.SIN_TABLE - EXE_BASE + TRIG_BYTES]
    atan = exe[ATAN_TABLE - EXE_BASE:ATAN_TABLE - EXE_BASE + ATAN_BYTES]
    cases = []
    modes = [M_UPDATE] * 8 + [M_AFTER] * 3 + [M_TUNING] + [M_START]
    attempts = 0
    while len(cases) < args.cases and attempts < args.cases * 3:
        attempts += 1
        c = make_case(exe, rnd, modes[attempts % len(modes)])
        if c:
            cases.append(c)
    n = len(FIELDS)
    out = bytearray(struct.pack('>6I', MAGIC, VERSION, n, len(cases), len(trig), len(atan)))
    out += trig + atan
    out += bytes(1 if f.skip else 0 for f in FIELDS)
    for mode, s7, row, before, after, events in cases:
        out += struct.pack('>2i', mode, s7)
        out += struct.pack('>12i', *row)
        out += struct.pack(f'>{n}i', *[v if -2**31 <= v < 2**31 else v - 2**32 for v in before])
        out += struct.pack(f'>{n}i', *[v if -2**31 <= v < 2**31 else v - 2**32 for v in after])
        evs = events[:EVENTS]
        out += struct.pack('>i', len(evs))
        for e in evs + [[0] * 6] * (EVENTS - len(evs)):
            out += struct.pack('>6i', *e)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    # the field names, for the checker
    (args.out.with_suffix('.fields')).write_text('\n'.join(f.name for f in FIELDS) + '\n')
    print(f'{len(cases)} cases, {n} fields -> {args.out}')


if __name__ == '__main__':
    main()
