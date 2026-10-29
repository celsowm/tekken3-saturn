#!/usr/bin/env python3
"""Export an original-code fight as a private replay case for t3_match_replay_eval.

Runs the original FightMain (Japan Rev.1) in the Tekken3Remake Unicorn harness
(third_party/Tekken3Remake/tools/research/fight_harness.py, PolyForm
Noncommercial) with its random bots, and writes what the native evaluator reads
from a PS1 capture, translated to the USA RAM layout through
tools/remake_import/layouts/usa.json:

  seed.bin         full 2 MiB USA-layout RAM before the seed frame (mapped
                   spans only, zero elsewhere)
  frame-N.ranges   T3RANGES snapshot after each later frame
  cases.tsv        the evaluator's input tape (original round flow columns)
  seed.json        scenario, seed frame and the four seed addresses
  ai.bin           with --cpu: every AiUpdate call (Rev.1 addresses): "T3AT",
                   u32 version (3), u32 CPU mask, the three bank bases, u32
                   count, u32 game mode (0x800AFF50), attract (0x800AE39C),
                   forced layout (0x800AE3D8), difficulty (u16 0x800AE6D0)
                   and CPU level (u8 0x800AE6D9);
                   per call u32 frame, u32 fighter, rand(), the AI generator,
                   AI self and opponent, u16 held and pressed pads, then the
                   AI block (0x8009F690, 0xFC0 bytes) at entry, then the
                   view at entry: per fighter its first 0x130 bytes, u32
                   +0x1A4, +0x3E8, +0x3EC, +0x3F0, +0x3F4, +0x3F8, +0x994,
                   u16 +0x1278 and +0x1886; then u32 frame counter and
                   g_fighterDistance (0x800AFF04)

By default, the harness's always-on presentation/latent fixes are undone
before setup. --remake-presentation-fixes reproduces legacy captures.

Everything written is derived from the game and stays under build/ or
assets/imported/. Run with third_party/Tekken3Remake/work/venv/bin/python.
"""
import argparse
import bisect
from contextlib import closing
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
LAYOUT = ROOT / 'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json'
USA_FIGHTERS = 0x800A9228
FIGHTER_SIZE = 0x188C
# The evaluator's seed addresses, USA layout of Rev.1 0x8009E9A8/0x8009E9B8
# (ground step accumulators X/Z), 0x8009EAA0 (the body's previous roots) and
# 0x80097E68 (the arena ramp), as prepare_psx_phase_capture.py maps them.
STEP_X, STEP_Z, PREVIOUS, RAMP = 0x8009E600, 0x8009E610, 0x8009E6F8, 0x80097A44
# USA spans read outside the fighter records (seed and comparisons).
GLOBALS = (
    (0x80095460, 0x54), (0x80096F2C, 0x14), (0x80097A44, 0x14), (0x8009BCA8, 0x2C),
    (0x8009E578, 0x28), (0x8009E5F8, 0x20), (0x8009E6FC, 0x1C), (0x800A8D6C, 4),
    (0x800ADBCC, 4), (0x800ADDFC, 2), (0x800ADE78, 2), (0x800ADF10, 2), (0x800ADF3C, 3),
    (0x800AFA58, 4), (0x800AFA90, 0x28),
    # The fighter view (Rev.1 0x800AE2E8, FUN_80036254) and the projection
    # distance H (0x800A919C): a draw before CameraFrame uses them.
    (0x800ADE20, 0x20), (0x800A8CD4, 4),
    (0x80097D10, 0x20),  # Original HudRoundUpdate's two 16-byte bar records.
    # Owned-camera audit: mapped Rev.1 camera, sources, director, RNG and targets.
    (0x800A85C0, 0x20), (0x800A02A8, 120), (0x800A8C68, 0x70),
    (0x800A39B8, 4), (0x800A912C, 4), (0x800ADCA0, 4),
    (0x800A9188, 0x30), (0x800ADCA8, 0x50),
    (0x80097048, 4),  # LatchButtonTaps' two previous entries (Rev.1 0x8009746C).
    (0x800971DC, 0x1A),  # FighterSounds timers/cooldowns/holds (Rev.1 0x80097600).
)
# Rev.1 pad words (prepare_psx_phase_capture.py): the mapped held/edge words
# InputSource reads, and RoundFlow's physical pad and pressed edges.
JP_PAD_HELD, JP_PAD_PRESSED = 0x800A9110, 0x800AE140
JP_PHYSICAL, JP_PHYSICAL_PRESSED = 0x800AE230, 0x800AE3D0
JP_FRAME_COUNTER = 0x800AFF14
FIGHT_SUBSTATE = 8
# CpuOrPadInput's AiUpdate and the input push it calls with the AI's pads.
AI_UPDATE, INPUT_PUSH, BRANCH_CONDITION = 0x80059B78, 0x8002C7D4, 0x8002E310
AI_BLOCK, AI_BLOCK_SIZE = 0x8009F690, 0xFC0
# The fighter fields AiUpdate reads beyond the first 0x130 bytes.
AI_VIEW_LOW, AI_VIEW_WORDS = 0x130, (0x1A4, 0x3E8, 0x3EC, 0x3F0, 0x3F4, 0x3F8, 0x994)
AI_FIGHTER_DISTANCE = 0x800AFF04
AI_RAND, AI_LCG, AI_SELF, AI_OPP, AI_BANKS = 0x800A3E80, 0x800AE168, 0x800AFF24, 0x800A8B44, 0x800AE0E8


class AiTape:
    """Records each AiUpdate's entry state and the pads it pushed."""

    def __init__(self, harness, frames, trace_frame=None):
        from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ
        from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_RA, UC_MIPS_REG_V0
        import fight_harness
        self.calls, self.pending, self.frames = [], None, frames
        cpu, fighters = harness.cpu, fight_harness.FIGHTERS
        # With trace_frame: every read AiUpdate makes in that frame ("R address
        # size value") and each BranchCondition's row and result ("C row 0
        # result"), whose own reads are left out.
        self.trace, tracing, condition = [], [False], [None]

        def read(uc, _access, address, size, _value, _data):
            if tracing[0] and condition[0] is None:
                self.trace.append(f'R {address:#x} {size} {int.from_bytes(cpu.read(address, size), "little"):#x}')

        def condition_entry(uc, _address, _size, _data):
            if tracing[0] and condition[0] is None:
                condition[0] = (uc.reg_read(UC_MIPS_REG_A0), uc.reg_read(UC_MIPS_REG_RA))

        def any_code(uc, address, _size, _data):
            if condition[0] is not None and address == condition[0][1]:
                self.trace.append(f'C {condition[0][0]:#x} 0 {int(uc.reg_read(UC_MIPS_REG_V0) != 0):#x}')
                condition[0] = None
        if trace_frame is not None:
            cpu.uc.hook_add(UC_HOOK_MEM_READ, read)
            cpu.uc.hook_add(UC_HOOK_CODE, condition_entry, begin=BRANCH_CONDITION, end=BRANCH_CONDITION)
            cpu.uc.hook_add(UC_HOOK_CODE, any_code)

        def entry(uc, _address, _size, _data):
            f = uc.reg_read(UC_MIPS_REG_A0)
            tracing[0] = self.frames[0] + 1 == trace_frame
            if tracing[0]:
                self.trace.append(f'A {fighters.index(f)}')
            view = b''
            for base in fighters:
                view += bytes(cpu.read(base, AI_VIEW_LOW))
                view += struct.pack('<7I', *(harness.u32(base + o) for o in AI_VIEW_WORDS))
                view += bytes(cpu.read(base + 0x1278, 2)) + bytes(cpu.read(base + 0x1886, 2))
            view += struct.pack('<2I', harness.u32(JP_FRAME_COUNTER), harness.u32(AI_FIGHTER_DISTANCE))
            self.pending = (self.frames[0] + 1, fighters.index(f), harness.u32(AI_RAND), harness.u32(AI_LCG),
                            harness.u32(AI_SELF), harness.u32(AI_OPP), bytes(cpu.read(AI_BLOCK, AI_BLOCK_SIZE)) + view)

        def push(uc, _address, _size, _data):
            f = uc.reg_read(UC_MIPS_REG_A0)
            if self.pending and fighters[self.pending[1]] == f:
                frame, index, rand, lcg, me, opp, block = self.pending
                self.calls.append(struct.pack('<6I2H', frame, index, rand, lcg, me, opp,
                                              uc.reg_read(UC_MIPS_REG_A2) & 0xFFFF,
                                              uc.reg_read(UC_MIPS_REG_A3) & 0xFFFF) + bytes(block))
                self.pending = None
                tracing[0] = False
        cpu.uc.hook_add(UC_HOOK_CODE, entry, begin=AI_UPDATE, end=AI_UPDATE)
        cpu.uc.hook_add(UC_HOOK_CODE, push, begin=INPUT_PUSH, end=INPUT_PUSH)

    def pads(self, frame, fighter):
        """The pads AiUpdate gave `fighter` in `frame`; 0 without a call (no health)."""
        for call in reversed(self.calls):
            at, index = struct.unpack_from('<2I', call)
            if at < frame:
                break
            if at == frame and index == fighter:
                return struct.unpack_from('<2H', call, 24)
        return 0, 0

    def write(self, path, mask, harness, first):
        calls = [c for c in self.calls if struct.unpack_from('<I', c)[0] >= first]
        banks = [harness.u32(AI_BANKS + 4 * k) for k in range(3)]
        settings = (harness.u32(0x800AFF50), harness.u32(0x800AE39C), harness.u32(0x800AE3D8),
                    harness.u32(0x800AE6D0) & 0xFFFF, harness.cpu.read(0x800AE6D9, 1)[0])
        path.write_bytes(b'T3AT' + struct.pack('<6I', 3, mask, *banks, len(calls)) + struct.pack('<5I', *settings)
                         + b''.join(calls))


class Layout:
    """USA -> Rev.1 addresses, from the layout's matched points (constant
    offsets per stretch; a span must not cross a change of offset)."""

    def __init__(self, path):
        points = json.loads(path.read_text())['blocks']['exe']['points']
        self.pairs = sorted((usa, jp) for jp, usa in points)
        self.usa = [usa for usa, _ in self.pairs]

    def to_usa(self, address):
        jp = sorted((j, u) for u, j in self.pairs)
        i = bisect.bisect_right([j for j, _ in jp], address) - 1
        return address + jp[i][1] - jp[i][0]

    def to_jp(self, address, size):
        i = bisect.bisect_right(self.usa, address) - 1
        if i < 0:
            raise ValueError(f'no layout point before {address:#x}')
        delta = self.pairs[i][1] - self.pairs[i][0]
        j = i + 1
        while j < len(self.pairs) and self.pairs[j][0] < address + size:
            if self.pairs[j][1] - self.pairs[j][0] != delta:
                raise ValueError(f'{address:#x}+{size:#x} crosses a layout offset change')
            j += 1
        return address + delta


def spans(fighters=True):
    out = list(GLOBALS)
    if fighters:
        out += [(USA_FIGHTERS + i * FIGHTER_SIZE, FIGHTER_SIZE) for i in range(2)]
    return out


def read_spans(harness, layout, wanted):
    """[(usa address, bytes)] for the wanted USA spans; the fighter records are
    read from the harness's own Rev.1 bases."""
    import fight_harness
    out = []
    for address, size in wanted:
        if size == FIGHTER_SIZE and address in (USA_FIGHTERS, USA_FIGHTERS + FIGHTER_SIZE):
            source = fight_harness.FIGHTERS[(address - USA_FIGHTERS) // FIGHTER_SIZE]
        else:
            source = layout.to_jp(address, size)
        out.append((address, harness.cpu.read(source, size)))
    return out


def write_ranges(path, chunks):
    body = b''.join(struct.pack('<II', a, len(d)) + d for a, d in chunks)
    path.write_bytes(b'T3RANGES' + struct.pack('<I', len(chunks)) + body)


def mapped_pad(held, pressed, side):
    """The mapped pad words of the original round flow (audit_psx_match_replay.py)."""
    sys.path.insert(0, str(ROOT / 'scripts'))
    from audit_psx_match_replay import mapped_pad_words
    return mapped_pad_words(held, pressed, side)


def neutral_facts(f):
    """The fields neutral() tests, by name, for diagnostics."""
    u16 = lambda o: struct.unpack_from('<H', f, o)[0]
    u32 = lambda o: struct.unpack_from('<I', f, o)[0]
    return dict(slot=u16(0xA0), pending=u32(0x1A4), row=u32(0x4C) == u32(0x54),
                w404=u16(0x404), w406=u16(0x406), blend_row=u32(0x1824) == u32(0x54),
                blend_mode=struct.unpack_from('<i', f, 0x181C)[0])


def neutral(harness):
    """The evaluator seeds only from both fighters (any character) standing in
    slot 3 with the live pose row as the blend's last row and no pending move."""
    import fight_harness
    for base in fight_harness.FIGHTERS:
        f = neutral_facts(harness.cpu.read(base, FIGHTER_SIZE))
        if (f['slot'] != 3 or f['pending'] or not f['row'] or f['w404'] or
                f['w406'] or not f['blend_row'] or f['blend_mode']):
            return False
    return True


def scripted_frames(scenario, path):
    """Drive the same original FightMain with a deterministic physical tape.

    Unlike random bots this can hold a command across intro exit or choose
    the initial stance at placement. Frames are the original main-loop clock.
    """
    import fight_harness as fh
    changes = {}
    for line in path.read_text().splitlines():
        words = line.partition('#')[0].split()
        if not words:
            continue
        frame, p0, p1 = (int(w, 0) for w in words)
        if frame < 1 or frame in changes or not 0 <= p0 <= 65535 or not 0 <= p1 <= 65535:
            raise ValueError('invalid or duplicate physical tape frame')
        changes[frame] = (p0, p1)
    h = fh.FightHarness(scenario.gameplay_fixes)
    over = []
    h.cpu.stub(fh.VS_END_HOOK, lambda c, *a: over.append(1) or 0)
    h.setup(scenario.chars, scenario.costumes, scenario.stage, scenario.seed,
            scenario.round_time, scenario.rounds, scenario.chip, scenario.key_tables,
            scenario.cpu, scenario.difficulty, scenario.level, scenario.attract)
    pads = (0, 0)
    for frame in range(1, scenario.max_frames + 1):
        pads = changes.get(frame, pads)
        sub = h.s32(fh.SUB_STATE)
        h.step(pads)
        yield h, pads, sub, list(h.calls)
        if over:
            return
    raise RuntimeError(f'{scenario.name}: physical tape did not finish the match')


def capture_frames(scenario, pad_script, retail=True):
    """Undo the harness's always-on presentation fixes before game setup.

    It patches camera bugs 1/53/54/55, CPU KO pads 59 and vibration 58 even
    with gameplay_fixes=False. Explicit retail=False reproduces old fixtures.
    No captured observation is rewritten.
    """
    import fight_harness as fh
    original = fh.FightHarness
    raw = (RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').read_bytes()
    sites = [(address, words) for address, words in fh._fixes()
             if not fh.TRAMPOLINES <= address < fh.GAMEPLAY_TRAMPOLINES]
    if sorted(a for a, _ in sites) != sorted((0x800640F8, 0x80063758, 0x80063830,
                                            0x800672A0, 0x8002C708, 0x80067330, 0x80075D60)):
        raise ValueError('presentation patch sites changed; review original-code restoration')
    class RetailHarness(original):
        def __init__(self, gameplay_fixes=False):
            if gameplay_fixes:
                raise ValueError('retail oracle cannot enable gameplay fixes')
            super().__init__(False)
            for address, words in sites:
                at = address - 0x80010000 + 0x800
                data = raw[at:at + 4*len(words)]
                if len(data) != 4*len(words):
                    raise ValueError('original patch site outside executable')
                self.cpu.write(address, data)
                if bytes(self.cpu.read(address, len(data))) != data:
                    raise ValueError('original patch restoration failed')
    try:
        if retail:
            fh.FightHarness = RetailHarness
        yield from (scripted_frames(scenario, pad_script) if pad_script else fh.record(scenario))
    finally:
        fh.FightHarness = original


def export(args):
    sys.path.insert(0, str(RESEARCH))
    import fight_harness
    layout = Layout(LAYOUT)
    for usa, jp in ((STEP_X, 0x8009E9A8), (STEP_Z, 0x8009E9B8), (PREVIOUS, 0x8009EAA0), (RAMP, 0x80097E68)):
        if layout.to_usa(jp) != usa or layout.to_jp(usa, 4) != jp:
            raise SystemExit(f'layout does not map {jp:#x} to {usa:#x}')
    for jp, usa, size in (
            (0x800A8A88, 0x800A85C0, 0x20), (0x800A0650, 0x800A02A8, 120),
            (0x800A9130, 0x800A8C68, 0x70), (0x800A3E80, 0x800A39B8, 4),
            (0x800A95F4, 0x800A912C, 4), (0x800A9650, 0x800A9188, 0x30),
            (0x800AE170, 0x800ADCA8, 0x50)):
        if layout.to_usa(jp) != usa or layout.to_jp(usa, size) != jp:
            raise SystemExit(f'layout does not map camera span {jp:#x}+{size:#x}')
    out = args.out
    out.mkdir(parents=True, exist_ok=False)
    scenario = fight_harness.Scenario(args.name, tuple(args.chars), tuple(args.costumes), args.stage,
                                      seed=args.seed, styles=tuple(args.styles), round_time=args.round_time,
                                      rounds=args.rounds, max_frames=args.max_frames, cpu=tuple(args.cpu),
                                      difficulty=args.difficulty, level=args.level)
    rows, seeded, seed_frame, frames = [], None, None, 0
    fight_frames, last_facts = 0, None
    counter, tape = [0], None
    with closing(capture_frames(scenario, args.pad_script, not args.remake_presentation_fixes)) as source:
        for harness, pads, sub, _calls in source:
            frames += 1
            counter[0] = frames
            if tape is None and any(args.cpu):
                tape = AiTape(harness, counter, args.ai_trace)
            if seeded is None:
                live = sub == FIGHT_SUBSTATE and harness.s32(fight_harness.SUB_STATE) == FIGHT_SUBSTATE
                if live:
                    fight_frames += 1
                    last_facts = [neutral_facts(harness.cpu.read(base, FIGHTER_SIZE)) for base in fight_harness.FIGHTERS]
                # The state after this frame is the seed when it is a neutral live fight frame.
                round_start = args.seed_round_start and sub == 7 and harness.s32(fight_harness.SUB_STATE) == FIGHT_SUBSTATE
                if (round_start or (not args.seed_round_start and live and neutral(harness))) and frames > args.skip:
                    ram = bytearray(0x200000)
                    for address, data in read_spans(harness, layout, spans()):
                        ram[address - 0x80000000:address - 0x80000000 + len(data)] = data
                    (out / 'seed.bin').write_bytes(ram)
                    sides = [ram[USA_FIGHTERS - 0x80000000 + i * FIGHTER_SIZE + 0xC6] for i in range(2)]
                    seeded, seed_frame = sides, frames
                continue
            if len(rows) >= args.ticks:
                break
            path = out / f'frame-{frames}.ranges'
            write_ranges(path, read_spans(harness, layout, spans()))
            u16 = lambda a: struct.unpack('<H', harness.cpu.read(a, 2))[0]
            # FighterUpdateSide runs before InputSource, so this frame's side maps its pads.
            sides = [harness.cpu.read(base + 0xC6, 1)[0] for base in fight_harness.FIGHTERS]
            words = []
            for i in range(2):
                if args.cpu[i]:
                    # CpuOrPadInput pushes AiUpdate's pads with side 0.
                    held, pressed = tape.pads(frames, i)
                    words += mapped_pad(held, pressed, 0)
                else:
                    words += mapped_pad(u16(JP_PAD_HELD + 2 * i), u16(JP_PAD_PRESSED + 2 * i), sides[i])
            flow = [u16(JP_PHYSICAL), u16(JP_PHYSICAL + 2), u16(JP_PHYSICAL_PRESSED), u16(JP_PHYSICAL_PRESSED + 2)]
            rows.append([str(path), *words, frames, harness.u32(JP_FRAME_COUNTER), *flow, *sides])
    if seeded is None:
        raise SystemExit(f'no neutral fight frame to seed from ({frames} frames, {fight_frames} live; '
                         f'last live fighters {last_facts})')
    (out / 'cases.tsv').write_text(''.join('\t'.join(map(str, r)) + '\n' for r in rows))
    if tape:
        tape.write(out / 'ai.bin', args.cpu[0] | args.cpu[1] << 1, harness, seed_frame + 1)
        if tape.trace:
            (out / 'ai-trace.txt').write_text(''.join(line + '\n' for line in tape.trace))
    (out / 'seed.json').write_text(json.dumps(dict(
        scenario=vars(args) | {'out': str(out), 'pad_script': str(args.pad_script) if args.pad_script else None},
        camera_state_captured=True, sound_state_captured=True,
        remake_presentation_fixes=args.remake_presentation_fixes,
        original_exe_sha256=hashlib.sha256((RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').read_bytes()).hexdigest(),
        seed_frame=seed_frame, sides=seeded, ticks=len(rows),
        step_x=STEP_X, step_z=STEP_Z, previous=PREVIOUS, ramp=RAMP), indent=2) + '\n')
    print(f'{out}: seed after frame {seed_frame}, {len(rows)} ticks')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--name', default='paul_paul')
    p.add_argument('--chars', type=int, nargs=2, default=(0, 0))
    p.add_argument('--costumes', type=int, nargs=2, default=(0, 1))
    p.add_argument('--stage', type=int, default=0)
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--styles', nargs=2, default=('random', 'random'))
    p.add_argument('--round-time', type=int, default=2)
    p.add_argument('--rounds', type=int, default=1)
    p.add_argument('--max-frames', type=int, default=20000)
    p.add_argument('--skip', type=int, default=0, help='seed no earlier than this frame')
    p.add_argument('--seed-round-start', action='store_true',
                   help='export from first-round setup, before the first FightFrame; no neutral-pose filter')
    p.add_argument('--pad-script', type=Path,
                   help='physical tape: main-loop frame, P1 word, P2 word (decimal or 0x hex)')
    p.add_argument('--ticks', type=int, default=3000, help='frames to export after the seed')
    p.add_argument('--cpu', type=int, nargs=2, default=(0, 0), help='1: that fighter is the CPU opponent')
    p.add_argument('--difficulty', type=int, default=1)
    p.add_argument('--level', type=int, default=4)
    p.add_argument('--ai-trace', type=int, metavar='FRAME',
                   help='with --cpu: write every AiUpdate read of FRAME to ai-trace.txt (slow)')
    p.add_argument('--remake-presentation-fixes', action='store_true',
                   help='legacy remake oracle: keep its presentation/CPU-KO fixes (not retail parity)')
    export(p.parse_args())


if __name__ == '__main__':
    main()
