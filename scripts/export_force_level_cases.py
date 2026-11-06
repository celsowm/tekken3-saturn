#!/usr/bin/env python3
"""Export golden cases of Tekken Force's level runner for tests/test_psx_force_level.c.

Runs the pinned Tekken3Remake's verified port (`force_sim.level_script` and `_enemy_slot`, the
script commands, the wait, end-of-level and boss states, the spawn pattern and the two enemy slots,
plus the score, hi-score and timer caps of `level_frame`) on random states, one step per case, and
writes each case's input and expected output as the flat vector of src/game/psx_force_level.h plus the
events the step requested. Drawing (text, items, key icons, progress bar) and projection are left out:
the screen position of the kill popup is an input. Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_force_level_cases.py \
        --levels assets/imported/force-packs --tables build/force-cases/TABLES.BIN \
        --out build/force-cases/level.bin --cases 3000

The script comes from LEVn.BIN (build_force_levels.py) and the constants from TABLES.BIN
(build_force_tables.py). The output is retail-derived and stays under build/.
"""
import argparse
import random
import struct
import sys
from pathlib import Path

RESEARCH = Path(__file__).resolve().parents[1] / 'third_party/Tekken3Remake/tools/research'
sys.path.insert(0, str(RESEARCH))

import draw_sim as ds            # noqa: E402
import force_sim as fs           # noqa: E402
import projection_sim as pj      # noqa: E402
import screens_sim as ss         # noqa: E402
from fight_sim import Ram       # noqa: E402

MAGIC = 0x54334643  # 'T3FC'
TABLE_BASE = fs.PATTERNS
TABLE_BYTES = 0x1D0
SCRIPT = 0x801F9000
FIGHTER = (0x801F6000, 0x801F6800)
PLAYER_FIGHTER = 0x800A96F0      # fighter record 0: its +0x3F4 is `player_health`
EVENTS = 16

# Event kinds (psx_force_level.h).
MUSIC_FADE, START_MOVE, ENEMY_ACTION, SOUND, MODEL_RESET, MODEL_REQUEST, MODEL_BIND, SAVE_PENDING, AREA_CHANGE, HEALTH_FLASH = range(1, 11)

# The flat vector, in the order of t3_force_level_save in psx_force_level.h.
GLOBALS = [
    ('level', fs.LEVEL, 'i'), ('state', fs.STATE, 'i'), ('clock', fs.CLOCK, 'i'), ('drawn', fs.DRAWN, 'i'),
    ('tally', fs.TALLY, 'i'), ('tally_done', fs.TALLY_DONE, 'i'), ('score', fs.SCORE, 'I'),
    ('hiscore_run', fs.HISCORE_RUN, 'I'), ('timer', fs.TIMER, 'i'), ('fight_start_timer', fs.FIGHT_START_TIMER, 'i'),
    ('keys', fs.KEYS, 'i'), ('key_earned', fs.KEY_EARNED, 'i'), ('final_challenge', fs.FINAL_CHALLENGE, 'i'),
    ('wait_defeated', fs.WAIT_DEFEATED, 'i'), ('wait_count', fs.WAIT_COUNT, 'i'), ('pattern_on', fs.PATTERN_ON, 'i'),
    ('pattern_stop_x', fs.PATTERN_STOP_X, 'i'), ('pattern_ptr', fs.PATTERN_PTR, 'p'), ('script_ptr', fs.SCRIPT_PTR, 's'),
    ('scroll_mode', fs.SCROLL_MODE, 'i'), ('scroll_param', fs.SCROLL_PARAM, 'i'), ('clear_time', fs.CLEAR_TIME, 'i'),
    ('bonus_time', fs.BONUS_TIME, 'i'), ('clear_bonus', fs.CLEAR_BONUS, 'i'),
    ('frozen', 0x800958B8, 'i'), ('text_off', ds.TEXT_OFF, 'i'), ('round_state', 0x80097350, 'i'),
    ('camera_x', fs.CAMERA + 0x14, 'i'), ('origin', 0x800B70E4, 'i'), ('player_health', 0x800A9AE4, 'i'),
    ('dr_b', 0x800982D0, 'b19'), ('player_index', None, 'pl'), ('lock_x', 0x800B7098 + 4, 'i'),
    ('mode_flags', ds.MODE + 0x40, 'B'), ('sub_mode', ds.MODE + 4, 'B'), ('flag_70e8', 0x800B70E8, 'i'),
    ('key_count', 0x800982F8, 'B'), ('kills', ds.MODE + 0x3C, 'H'), ('hiscore_flag', ds.MODE + 0x3F, 'B'),
    ('bound0', 0x800B7098 + 0x28, 'i'), ('bound1', 0x800B7098 + 0x2C, 'i'), ('bound2', 0x800B7098 + 0x30, 'i'),
    ('bound3', 0x800B7098 + 0x34, 'i'), ('free_scroll', 0x800B70C4, 'i'),
]
SLOT = [('state', 8, 'h'), ('timer', 0xC, 'H'), ('type', 0xE, 'h'), ('loaded', 0x10, 'h'), ('requested', 0x12, 'H'),
        ('popup_x', 4, 'H'), ('popup_y', 6, 'H'), ('popup_until', 0x14, 'H')]
FIGHTER_FIELDS = [('health', 0x3F4, 'i'), ('health_max', 0x3F8, 'i'), ('f04', 4, 'i'), ('f92', 0x92, 'H'),
                  ('fb8', 0xB8, 'B'), ('fc2', 0xC2, 'B'), ('fc3', 0xC3, 'B'), ('fc4', 0xC4, 'B'), ('fce', 0xCE, 'B'),
                  ('fe7', 0xE7, 'B'), ('owner', 0x22, 'B'), ('index', 0x1E, 'B'), ('key', 0x14, 'H'),
                  ('anchor_x', 0, 'i'), ('anchor_z', 8, 'i'), ('root_x', 0xF68, 'i'), ('root_z', 0xF70, 'i'),
                  ('screen_x', 0x990, 'i'), ('screen_y', 0x998, 'i')]
ITEM = [('state', 0, 'I'), ('x', 4, 'i'), ('z', 0xC, 'i'), ('popup_x', 0x14, 'H'), ('popup_y', 0x16, 'H'), ('until', 0x18, 'i')]
PLAYER_FIELDS = [('x', 0xF68, 'i'), ('z', 0xF70, 'i'), ('db', 0xDB, 'B'), ('screen_x', 0x990, 'i'), ('screen_z', 0x998, 'i')]
VECTOR_WORDS = len(GLOBALS) + 2 * len(SLOT) + 2 * len(FIGHTER_FIELDS) + 2 * len(ITEM) + len(PLAYER_FIELDS)


class Log(fs.ForceHooks):
    def __init__(self):
        self.events = []

    def music_fade(self, ram, a): self.events.append((MUSIC_FADE, a, 0, 0))
    def start_move(self, ram, fighter, slot): self.events.append((START_MOVE, FIGHTER.index(fighter) if fighter in FIGHTER else -1, slot, 0))
    def enemy_action(self, ram, a, b, c): self.events.append((ENEMY_ACTION, a, b, c))
    def sound(self, ram, sound_id): self.events.append((SOUND, sound_id, 0, 0))
    def model_reset(self, ram, fighter): self.events.append((MODEL_RESET, FIGHTER.index(fighter), 0, 0))
    def model_request(self, ram, index, key): self.events.append((MODEL_REQUEST, index, key, 0))
    def model_parts(self, ram, index): return 0, 0
    def model_bind(self, ram, fighter, a, b): self.events.append((MODEL_BIND, FIGHTER.index(fighter), 0, 0))


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v >= 1 << 31 else v


def read_vector(ram, player_index):
    out = []
    for name, addr, kind in GLOBALS:
        if kind == 'pl':
            out.append(player_index)
        elif kind == 'p':
            out.append(ram.u32(addr) - TABLE_BASE)
        elif kind == 's':
            out.append((ram.u32(addr) - SCRIPT) // 20)
        elif kind == 'b19':
            out.append(ram.u32(addr) >> 19 & 1)
        else:
            out.append(s32(ram.get(addr, kind)))
    for i in range(2):
        for name, off, kind in SLOT:
            out.append(s32(ram.get(fs.SLOTS + 0x18 * i + off, kind)))
    for base in FIGHTER:
        for name, off, kind in FIGHTER_FIELDS:
            out.append(s32(ram.get(base + off, kind)))
    for i in range(2):
        for name, off, kind in ITEM:
            out.append(s32(ram.get(fs.ITEMS + 0xBC * i + off, kind)))
    for name, off, kind in PLAYER_FIELDS:
        out.append(s32(ram.get(PLAYER_FIGHTER + off, kind)))
    return out


def write_vector(ram, vec):
    it = iter(vec)
    player_index = 0
    for name, addr, kind in GLOBALS:
        v = next(it)
        if kind == 'pl':
            player_index = v
        elif kind == 'p':
            ram.put(addr, 'I', TABLE_BASE + v)
        elif kind == 's':
            ram.put(addr, 'I', SCRIPT + 20 * v)
        elif kind == 'b19':
            ram.put(addr, 'I', v << 19)
        else:
            ram.put(addr, kind, v)
    for i in range(2):
        ram.put(fs.SLOTS + 0x18 * i, 'I', FIGHTER[i])
        for name, off, kind in SLOT:
            ram.put(fs.SLOTS + 0x18 * i + off, kind, next(it))
    for base in FIGHTER:
        for name, off, kind in FIGHTER_FIELDS:
            ram.put(base + off, kind, next(it))
    for i in range(2):
        for name, off, kind in ITEM:
            ram.put(fs.ITEMS + 0xBC * i + off, kind, next(it))
    for name, off, kind in PLAYER_FIELDS:
        ram.put(PLAYER_FIGHTER + off, kind, next(it))
    ram.put(fs.PLAYER, 'I', PLAYER_FIGHTER)
    ram.put(fs.PLAYER_FIGHTER, 'I', PLAYER_FIGHTER)
    ram.put(PLAYER_FIGHTER + 0x1E, 'B', player_index)
    return player_index


def step(ram, log):
    """level_frame without its drawing: the script, both slots and the caps."""
    player = ram.u32(fs.PLAYER)
    if fs.level_script(ram, None):
        fs.HOOKS.events.append((AREA_CHANGE, 0, 0, 0))
    for i in range(2):
        fs._enemy_slot(ram, None, fs.SLOTS + 0x18 * i, player)
    fs.items(ram, None)
    if fs.SCORE_CAP < ram.u32(fs.SCORE):
        ram.put(fs.SCORE, 'I', fs.SCORE_CAP)
    if ram.u32(fs.HISCORE_RUN) < ram.u32(fs.SCORE):
        ram.put(ds.MODE + 0x3F, 'B', 1)
        ram.put(fs.HISCORE_RUN, 'I', ram.u32(fs.SCORE))
    if ram.s32(fs.TIMER) >= 0x1735:
        ram.put(fs.TIMER, 'I', 0x1734)
    if ram.u32(0x800958B8) == 0 and ram.u32(0x80097350) == 1:
        t = ram.s32(fs.TIMER)
        if t in (60, 120, 180, 240, 300):
            fs.HOOKS.sound(ram, ram.u16(fs.COUNTDOWN_SOUNDS + 2 * (t // 60 - 1)))


def random_vector(rng, scripts):
    level = rng.randrange(4)
    script = scripts[level]
    # records up to the level's end command (6): the runner never reads past it
    records = next(i for i in range(len(script) // 20) if struct.unpack_from('<h', script, 20 * i + 4)[0] == 6) + 1
    index = rng.choice([0, 0, rng.randrange(records), rng.randrange(records)])
    trigger = struct.unpack_from('<i', script, 20 * index)[0]
    origin = rng.choice([0, rng.randrange(-5000, 5000)])
    progress = rng.choice([trigger - rng.randrange(1, 300), trigger, trigger + rng.randrange(0, 600), rng.randrange(-100, 30000)])
    v = {name: 0 for name, _, _ in GLOBALS}
    v.update(level=level, state=rng.choice([0, 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 9, 10]),
             clock=rng.choice([0, 1, 100, rng.randrange(5000)]), drawn=rng.randrange(3000),
             tally=rng.choice([0, 0x1D, 0x1E, 0x3B, 0x3C, 0x59, 0x5A, 0x77, 0x78, 0x79, 0xB4, 0xB5, rng.randrange(-5, 5000)]),
             tally_done=rng.randrange(2), score=rng.choice([rng.getrandbits(20), fs.SCORE_CAP, fs.SCORE_CAP + 1, rng.getrandbits(32)]),
             timer=rng.choice([60, 120, 180, 240, 300, 0x1734, 0x1735, 0x2000, -60, rng.randrange(0x1734)]),
             fight_start_timer=rng.randrange(0x1734), keys=rng.randrange(4), key_earned=rng.randrange(2),
             final_challenge=rng.randrange(2), wait_defeated=rng.randrange(4), wait_count=rng.choice([-1, 0, 1, 2, 3]),
             pattern_on=rng.choice([0, 1, 1, 2]), pattern_stop_x=rng.choice([0, 3000, 20000]),
             pattern_ptr=6 * rng.randrange(0, 40), script_ptr=index, scroll_mode=rng.randrange(3),
             scroll_param=rng.randrange(3), clear_time=rng.randrange(20000), bonus_time=rng.choice([0, 1, 2400, rng.randrange(-100, 4000)]),
             clear_bonus=rng.choice([0, 100, 12300]), frozen=rng.choice([0, 0, 0, 1]), text_off=rng.choice([0, 0, 1]),
             round_state=rng.choice([1, 1, 1, 8, 0, 3]), camera_x=origin + progress, origin=origin,
             player_health=rng.choice([0, 65536 * 120, rng.randrange(0, 1 << 24), -5]), dr_b=rng.randrange(2),
             player_index=rng.randrange(3), lock_x=rng.randrange(-5000, 50000), mode_flags=rng.getrandbits(8),
             sub_mode=rng.randrange(2), flag_70e8=rng.randrange(2), key_count=rng.choice([0, 1, 254, 255]),
             kills=rng.choice([0, 7, 65535]), hiscore_flag=rng.randrange(2), hiscore_run=rng.choice([0, rng.getrandbits(20), rng.getrandbits(32)]))
    vec = [v[name] for name, _, _ in GLOBALS]
    for _ in range(2):
        st = rng.choice([0, 1, 1, 2, 3, 4, 5, 5, 6, 6, 7])
        t = rng.choice([0, 1, rng.randrange(5000)])
        vec += [st, t, rng.randrange(5), rng.randrange(2), rng.randrange(2), rng.getrandbits(16), rng.getrandbits(16),
                rng.choice([0, t, rng.randrange(5000)])]
    for _ in range(2):
        health = rng.choice([0, 1, -3, 65536 * 30, rng.randrange(0, 1 << 22)])
        vec += [health, rng.randrange(1, 1 << 22), rng.getrandbits(32) & 0x7FFFFFFF, rng.getrandbits(16), rng.getrandbits(8),
                rng.getrandbits(8), rng.getrandbits(8), rng.randrange(2), rng.randrange(2), rng.randrange(2), rng.randrange(3),
                rng.randrange(3), rng.getrandbits(16), rng.randrange(-9000, 9000), rng.randrange(-3000, 3000),
                rng.randrange(-9000, 9000), rng.randrange(-3000, 3000), rng.randrange(-3000, 3000), rng.randrange(-3000, 3000)]
    px = origin + progress
    for _ in range(2):
        st = rng.choice([0, 1, 1, 1, 2, 2])
        ix = rng.choice([px - 6000, px, px + 100, rng.randrange(-6000, 40000)])
        clock = vec[2]
        vec += [st, ix, rng.randrange(-2000, 2000), rng.getrandbits(16), rng.getrandbits(16),
                rng.choice([clock + 0xF, clock + 1, clock - 3, rng.randrange(0, 6000), clock + 0x3C])]
    pxx = rng.choice([px, px + 100, px + 600, rng.randrange(-5000, 40000)])
    vec += [pxx, rng.choice([0, 100, 600, rng.randrange(-2000, 2000)]), rng.choice([0, 0, 1]),
            rng.randrange(-3000, 3000), rng.randrange(-3000, 3000)]
    return [s32(x) for x in vec], level


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--levels', type=Path, required=True, help='directory with LEV1-4.BIN')
    parser.add_argument('--tables', type=Path, required=True, help='TABLES.BIN (build_force_tables.py)')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=3000)
    parser.add_argument('--seed', type=int, default=1)
    args = parser.parse_args()
    scripts = []
    for n in range(1, 5):
        d = (args.levels / f'LEV{n}.BIN').read_bytes()
        h = struct.unpack_from('>13I', d)
        scripts.append(d[h[11]:h[11] + h[12]])
    tables = args.tables.read_bytes()
    assert len(tables) == TABLE_BYTES
    rng = random.Random(args.seed)
    log = Log()
    fs.HOOKS = log
    ds.text = lambda *a: None
    ds.link = lambda *a: None
    fs.health_flash = lambda ram, f: log.events.append((HEALTH_FLASH, 0, 0, 0))
    ss.save_pending = lambda ram: log.events.append((SAVE_PENDING, 0, 0, 0))

    def screen_of(ram, gte, x, y, z, scratch):
        return x, z, 0
    pj.screen_of = screen_of
    out = bytearray(struct.pack('>6I', MAGIC, 1, VECTOR_WORDS, EVENTS, args.cases, TABLE_BYTES))
    out += tables
    for level in range(4):
        out += struct.pack('>I', len(scripts[level])) + scripts[level]
    for _ in range(args.cases):
        vec, level = random_vector(rng, scripts)
        ram = Ram(bytes(0x200000))
        ram.data[ram._off(TABLE_BASE):ram._off(TABLE_BASE) + TABLE_BYTES] = tables
        ram.data[ram._off(SCRIPT):ram._off(SCRIPT) + len(scripts[level])] = scripts[level]
        # the golden screen position is the fighter's +0x990 / +0x998 (see screen_of)
        write_vector(ram, vec)
        for i in range(5):
            ram.put(fs.COUNTDOWN_SOUNDS + 2 * i, 'H', 0x4C60 + i)
        log.events = []
        step(ram, log)
        after = read_vector(ram, vec[[n for n, _, _ in GLOBALS].index('player_index')])
        events = log.events[:EVENTS]
        out += struct.pack('>I', level)
        out += struct.pack(f'>{VECTOR_WORDS}i', *vec)
        out += struct.pack(f'>{VECTOR_WORDS}i', *after)
        out += struct.pack('>I', len(events)) + b''.join(struct.pack('>4i', *e) for e in events)
        out += bytes(16 * (EVENTS - len(events)))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))
    print(f'{args.cases} cases, {VECTOR_WORDS} words each -> {args.out}')


if __name__ == '__main__':
    main()
