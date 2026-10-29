#!/usr/bin/env python3
"""FighterSounds state, random draws and ordered replay events vs original MIPS.

Audio playback and VOICE_OFF are stubbed; no audio/render parity is implied.
Tables independently normalized from the original JP executable must equal
the fingerprinted USA import before C is compared with MIPS.
"""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    a = parser.parse_args()
    exe = RESEARCH.parent.parent / 'work/jp_rev1/exe.bin'
    tables = ROOT / 'assets/imported/tekken3-usa/pose-tables/psx_sounds.bin'
    if not exe.is_file() or not tables.is_file():
        print('private sound tables/executable absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    sys.path.insert(0, str(ROOT / 'scripts'))
    try:
        from psxcpu import load_release
    except ImportError:
        print('original harness dependencies absent; skipped')
        return 77
    from extract_psx_sound_tables import normalize
    assert tables.read_bytes() == normalize(exe.read_bytes())
    # Compare the sound words of EVERY raw/compacted move, not just the
    # commands selected by this fight. Indices may change; executed words may not.
    from divmot_to_logic import sections_of
    banks = [(ROOT / 'assets/imported/tekken3-usa/raw/074.bin', 74, 0),
             (ROOT / 'assets/imported/law-camera/raw/082.bin', 82, 1),
             (ROOT / 'assets/imported/tekken3-usa/raw/279.bin', 279, 99)]
    compared = 0
    for raw_path, record, kind in banks:
        compact_path = ROOT / f'assets/imported/tekken3-usa/divmot/{record:03d}/bank_{kind}.t3logic'
        if not raw_path.is_file() or not compact_path.is_file():
            continue
        raw, compact = raw_path.read_bytes(), compact_path.read_bytes()
        count, raw_offsets = sections_of(raw)
        compact_count, compact_offsets = sections_of(compact)
        assert count == compact_count
        if compact_offsets[3] == compact_offsets[4]:
            continue  # Older private fixture lacks opt-in sound commands.
        def words(data, offsets, row):
            index = struct.unpack_from('<I', data, offsets[1] + 56*row + 28)[0]
            if index == 0xFFFFFFFF:
                return None
            result = []
            for n in range(3):
                value = struct.unpack_from('<H', data, offsets[3] + 2*(index+n))[0]
                result.append(value)
                if value == 0xFFFF:
                    break
            return result
        for row in range(count):
            assert words(raw, raw_offsets, row) == words(compact, compact_offsets, row)
            compared += 1
    cpu = load_release()
    events = []
    def record(_cpu, kind, player, code, *unused):
        events.append([kind, player, code & 65535])
        return 0
    cpu.stub(0x80033780, record)
    for address in (0x8004B9B8, 0x80040F28, 0x8004108C, 0x80040E98, 0x80075BFC):
        cpu.stub(address, lambda c, *args: 0)
    phase = [2]
    cpu.stub(0x8002BDDC, lambda c, *args: int(phase[0] in (0, 1, 2, 5)))
    fighters = (0x800A96F0, 0x800AAF7C)
    def write(address, fmt, *values):
        cpu.write(address, struct.pack('<' + fmt, *values))
    rng = random.Random(0x41138)
    cases = []
    # Ambient attack selection: all voice sets, RNG branches, cooldown and
    # signed timer boundaries, including Mokujin's zero-count shout list.
    for voice in range(23):
        for seed in range(16):
            state = [[-1, 0, 0, 0, 0, 0, 0], [-1, 0, 0, 0, 0, 0, 0]]
            facts = [[p, voice, voice, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                      140 << 16, 12, 0, 1, 0, 10, 0, 0xFFFF, 0xFFFF, 0xFFFF] for p in range(2)]
            cases.append([seed, 1, 0, 2, *sum(state, []), *sum(facts, [])])
    commands = ([], [0x7000], [0x7041], [0x7045], [0x7043], [0x2000], [0x3000],
                [0x4000], [0x5000], [0x9000], [0x9001], [0x9002], [0x9003],
                [0x8001], [0x800A], [0x2000, 0x3000, 0x9003])
    for script in range(470):
        for frame in (1, 30, 120):
            state = [[script, script, 0, frame - 1, 0, 0, 0], [-1, 0, 0, 0, 0, 0, 0]]
            facts = [[p, p, p, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      140 << 16, 0, 0, 1, 0, 10, 0, 0xFFFF, 0xFFFF, 0xFFFF] for p in range(2)]
            cases.append([1, frame, frame, 2, *sum(state, []), *sum(facts, [])])
    for n in range(1100):
        states, facts = [], []
        for p in range(2):
            script = rng.choice([-1, -1, rng.randrange(470)])
            states.append([script, script if script >= 0 else 0, 0, rng.randrange(-1, 100),
                           rng.choice([-1, 0, 0, 0, 1, 10, 32767]), rng.choice([-1, 0, 0, 1, 40]), rng.randrange(2)])
            cmd = list(rng.choice(commands))
            count = len(cmd)
            cmd += [0xFFFF] * (3 - len(cmd))
            facts.append([p, rng.choice([0, 1, 5, 15, 16, 18]), rng.randrange(23), rng.choice([0, 10, 11, 12, 16]),
                          rng.randrange(2), rng.randrange(2), rng.randrange(2), rng.randrange(2), rng.randrange(2),
                          rng.randrange(2), rng.randrange(2), rng.randrange(2), rng.randrange(2),
                          rng.choice([0, 1, 140 << 16]), rng.choice([-1, 0, 6, 7, 12, 13, 19, 20, 22, 23, 32, 33]),
                          rng.choice([-1, 0, 0, 1]), rng.choice([1, 2, 9, 10, 12, 20]), 9, 10, count, *cmd])
        cases.append([rng.getrandbits(32), rng.randrange(4), rng.randrange(4), rng.randrange(8),
                      *sum(states, []), *sum(facts, [])])
    expected = []
    for case in cases:
        seed, frame, vblank, phase[0] = case[:4]
        write(0x800A3E80, 'I', seed)
        write(0x800AFF14, 'I', frame)
        write(0x800AE6E0, 'I', vblank)
        for p, fighter in enumerate(fighters):
            s, f = case[4 + 7*p:11 + 7*p], case[18 + 23*p:41 + 23*p]
            cpu.write(fighter, bytes(0x188C))
            write(fighter + 0x12, 'h', p)
            write(fighter + 0x18, 'hh', f[1], f[2])
            write(fighter + 0x70, 'hh', s[0], s[1])
            write(fighter + 0x11E, 'hh', s[2], s[3])
            write(0x8009760C + 2*p, 'h', s[4])
            write(0x80097600 + 4*p, 'i', s[5])
            write(0x80097614 + 2*p, 'h', s[6])
            write(0x800958CC + 4*p, 'I', f[12])
            write(fighter + 0x3F4, 'i', f[13])
            write(fighter + 0x5E, 'h', f[14])
            write(fighter + 0x74, 'h', f[15])
            write(fighter + 0x58, 'h', f[16])
            for offset, value in ((0xDE,f[4]), (0xD0,f[5]), (0xCE,f[6]), (0xD1,f[7]),
                                  (0xCF,f[8]), (0xDA,f[9]), (0xDC,f[10]), (0xDD,f[11])):
                write(fighter + offset, 'B', value)
            move, desc, cmd = 0x801D0000 + 0x100*p, 0x801D0400 + 0x100*p, 0x801D0800 + 0x100*p
            cpu.write(move, bytes(56))
            write(fighter + 0x54, 'I', move)
            write(move + 0x28, 'I', desc)
            write(desc, 'B', f[3])
            write(move + 0x19, 'B', f[17])
            write(move + 0x2D, 'B', f[18])
            write(move + 0x1C, 'I', cmd if f[19] else 0)
            write(cmd, '3H', *f[20:23])
        events.clear()
        cpu.call(0x80041138, *fighters)
        out_states = []
        for p, fighter in enumerate(fighters):
            out_states.append([*struct.unpack('<2h', cpu.read(fighter + 0x70, 4)),
                               *struct.unpack('<2h', cpu.read(fighter + 0x11E, 4)),
                               struct.unpack('<h', cpu.read(0x8009760C + 2*p, 2))[0],
                               struct.unpack('<i', cpu.read(0x80097600 + 4*p, 4))[0],
                               struct.unpack('<h', cpu.read(0x80097614 + 2*p, 2))[0]])
        expected.append(dict(events=list(events), ok=1, state=out_states,
                             random=struct.unpack('<I', cpu.read(0x800A3E80, 4))[0]))
    result = subprocess.run([str(a.runner.resolve()), str(tables)], input=b''.join(
        struct.pack('<64I', *(v & 0xFFFFFFFF for v in case)) for case in cases), capture_output=True, check=True)
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(actual) == len(expected)
    for index, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            raise AssertionError(f'case {index}: {cases[index]}\noriginal {want}\nnative {got}')
    print(f'{len(cases)} original MIPS fighter sound cases matched: state, RNG, ordered replay events; '
          f'{compared} raw/compacted move sound lists identical')
    return 0


if __name__ == '__main__':
    sys.exit(main())
