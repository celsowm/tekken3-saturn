#!/usr/bin/env python3
"""Turn a Tekken3Remake golden fight trace into camera test cases.

Reads `work/traces/fight_<name>.bin` (fight_harness.write_trace) from the
pinned Tekken3Remake checkout and writes, per frame, the fighter fields the
camera director reads plus the camera's RAM after the frame. The move-flag
word (+0x24 of the running move row) needs the motion banks, which the trace
only names, so the banks are dumped once from the harness after the first
frame. Output is private (git-ignored under build/): it is derived from the
game's RAM.

Format, little endian: "T3CC", u32 version 3, u32 frames, i32 intro offsets
[10][3], i32 reserved (0), i32 presets[30][10], i32 preset_index[30],
i32 outside_blend_out, then three choice lists (EXE, fighter 0's bank,
fighter 1's bank), each i32 count and count x (kind, weight, script); then
per fighter bank its camera streams: u32 section 8 address, i32 count, count x (offset, frames,
yaw mode, first sample), i32 sample count, samples x 7 i32 (decoded with the
research port camera_scripts.py); then the hit cameras: i32 rows[4][3], per
list (3) i32 count and count x (cumulative threshold, script index x 4),
scripts[7] x 14 i32 (kind, frames, blend in, blend out, start, end distance,
pitches[4], yaws[4]); the winner camera's i32 lateral[3], forward[3],
height[3]; the shake scripts, 3 x (u32 address, 96 bytes); then per frame:
  i32 fighter[2][29]  pos_x pos_z root_x root_z head_y screen_x throw_state
                      pose_frame cur_slot bank_type move_flags move_id
                      move_state event_left_b event_left_c transition
                      heading pos_y hit_clean counter_hit close_hit ko
                      last_damage hit_point[3] facing root_y health
  i32 fighter_distance, camera_phase, rounds_played (all before the frame)
  raw ranges after the frame: RANGES below, in order.
"""
import argparse
import ast
import json
import struct
import sys
import zlib
from pathlib import Path

from psx_camera_tables import Exe, research, spline_tables, streams, tables as camera_tables

ROOT = Path(__file__).resolve().parents[1]
REMAKE = ROOT / "third_party" / "Tekken3Remake"
RANGES = (("fight_flags", 0x80095840, 0xC0), ("camera_lift", 0x80098750, 0x10),
          ("intro_counter", 0x80098920, 0x10), ("camera_sources", 0x800A0640, 0x240),
          ("camera", 0x800A8A80, 0x28), ("director", 0x800A9130, 0x80),
          ("panorama", 0x800A9650, 0x30), ("look_point", 0x800AE0D8, 0xC),
          ("fight_globals", 0x800AE140, 0x70), ("view", 0x800AE438, 0x20),
          ("mode", 0x800AFF00, 0x80), ("round", 0x80097340, 0x170), ("rand", 0x800A3E80, 4),
          ("winner", 0x8009EA40, 0x60), ("camera_rng", 0x800A95F4, 4), ("shake", 0x80097EC4, 4))
BANK_LO, BANK_HI = 0x800F9EA0, 0x80200000
CHOICE_SECTION, STREAM_SECTION = 7, 8  # bank sections: camera choices, camera streams
CAMERA_PHASE, ROUNDS_PLAYED, FIGHTER_DISTANCE = 0x80095890, 0x800958A8, 0x800AFF04


def read_trace(path: Path):
    d = path.read_bytes()
    assert d[:4] == b"T3FT", "not a fight trace"
    hl = struct.unpack_from("<I", d, 8)[0]
    header = json.loads(d[12:12 + hl])
    at = 12 + hl
    blen, zlen = struct.unpack_from("<II", d, at)
    body = zlib.decompress(d[at + 8:at + 8 + zlen])
    assert len(body) == blen
    for key in ("ranges", "bank_epochs"):
        if isinstance(header[key], str):
            header[key] = ast.literal_eval(header[key])
    return header, body


def frames(header, body):
    size = header["fighter_size"]
    ranges = header["ranges"]
    at = 0
    for _ in range(header["frames"]):
        at += 4
        sub, calls = struct.unpack_from("<bB", body, at)
        at += 2
        for _ in range(calls):
            _, n = struct.unpack_from("<BB", body, at)
            at += 2 + 4 * n
        fighters = body[at:at + size], body[at + size:at + 2 * size]
        at += 2 * size
        ram = {}
        for name, addr, length in ranges:
            ram[name] = (addr, body[at:at + length])
            at += length
        yield fighters, ram
    assert at == len(body)


def ram_read(ram, addr, fmt):
    for base, data in ram.values():
        if base <= addr and addr + struct.calcsize(fmt) <= base + len(data):
            return struct.unpack_from("<" + fmt, data, addr - base)[0]
    raise KeyError(hex(addr))


def dump_banks(scenario_name: str) -> bytes:
    sys.path.insert(0, str(REMAKE / "tools" / "research"))
    import fight_harness as fh
    scenario = next(s for s in fh.SCENARIOS if s.name == scenario_name)
    for h, *_ in fh.record(scenario):
        return h.cpu.read(BANK_LO, BANK_HI - BANK_LO)
    raise RuntimeError("no frame")


def bank_u32(banks: bytes, addr: int) -> int:
    off = addr - BANK_LO
    if not 0 <= off <= len(banks) - 4:
        raise ValueError(f"{addr:#x} outside the dumped banks")
    return struct.unpack_from("<I", banks, off)[0]


def event_left(banks: bytes, move: int, frame: int, kind: int) -> int:
    """Frames from the pose frame to the move's last event `kind` (high byte), -1 none."""
    at = bank_u32(banks, move + 0x20)   # relocated: a pointer into section 4, 0 for none
    if at == 0:
        return -1
    left = -1
    while True:
        word, command = struct.unpack_from("<HH", banks, at - BANK_LO)
        if word == 0:
            return left
        if command & 0xFF00 == kind:
            left = word - frame
        at += 4


def choice_list(banks: bytes, bank: dict) -> list[tuple[int, int, int]]:
    lo, hi = bank["sections"][CHOICE_SECTION], bank["sections"][CHOICE_SECTION + 1]
    return [struct.unpack_from("<hHi", banks, a - BANK_LO) for a in range(lo, hi - 7, 8)]


def camera_streams(banks: bytes, bank: dict, choices) -> tuple[list, list]:
    """Every stream a choice list names, sampled per frame by the research decoder."""
    motion = research("motion")
    tab = spline_tables(Exe.jp().read(motion.SPLINE_TABLE_BASE - 0x40, 0x660))
    return streams(banks, bank["sections"][STREAM_SECTION] - BANK_LO, choices, tab)


def hit_tables(t: dict) -> bytes:
    out = bytearray(struct.pack("<12i", *t["hit_rows"]))
    for records in t["hit_lists"]:
        out += struct.pack("<i", len(records))
        for r in records:
            out += struct.pack("<5i", *r)
    for script in t["hit_scripts"]:
        out += struct.pack("<14i", *script)
    return bytes(out)


FIGHTERS = (0x800A96F0, 0x800AAF7C)


def hit_point(rec: bytes, index: int) -> tuple:
    """The contact point of the best hit slot; +0x194 points at it."""
    at = struct.unpack_from("<I", rec, 0x194)[0] - FIGHTERS[index]
    return struct.unpack_from("<3i", rec, at) if 0x13C <= at <= 0x13C + 0x2C else (0, 0, 0)


def fighter_fields(rec: bytes, banks: bytes, index: int):
    i = lambda o: struct.unpack_from("<i", rec, o)[0]
    h = lambda o: struct.unpack_from("<h", rec, o)[0]
    move = struct.unpack_from("<I", rec, 0x54)[0]
    frame = h(0x58)
    return (i(0x0), i(0x8), i(0xF68), i(0xF70), i(0x994), h(0x1278), h(0x74), frame,
            h(0xA0), h(0x16), bank_u32(banks, move + 0x24), move, bank_u32(banks, move + 4),
            event_left(banks, move, frame, 0xB00), event_left(banks, move, frame, 0xC00),
            h(0x19A), h(0x2C), i(0x4), rec[0xD0], rec[0xD3], rec[0xD4], rec[0xDE], h(0xE8),
            *hit_point(rec, index), h(0xE), i(0xF6C), i(0x3F4))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fight", default="paul_law")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    out = args.out or ROOT / "build" / "camera-trace" / f"{args.fight}.cases"
    header, body = read_trace(REMAKE / "work" / "traces" / f"fight_{args.fight}.bin")
    if len(header["bank_epochs"]) != 1:
        raise SystemExit("traces with bank reloads are not supported")
    banks = dump_banks(args.fight)
    epoch = header["bank_epochs"][0]["banks"]
    t = camera_tables(Exe.jp())
    blob = bytearray(b"T3CC" + struct.pack("<II", 3, header["frames"]))
    blob += struct.pack("<30i", *t["intro_offsets"])
    blob += struct.pack("<i", 0)
    blob += struct.pack("<300i", *t["presets"])
    blob += struct.pack("<30i", *t["preset_index"])
    blob += struct.pack("<i", t["outside_blend_out"])
    lists = [t["choices"], choice_list(banks, epoch[0]), choice_list(banks, epoch[1])]
    for entries in lists:
        blob += struct.pack("<i", len(entries))
        for e in entries:
            blob += struct.pack("<3i", *e)
    for k in range(2):
        entries, samples = camera_streams(banks, epoch[k], lists[1 + k])
        blob += struct.pack("<I", epoch[k]["sections"][STREAM_SECTION])
        blob += struct.pack("<i", len(entries))
        for e in entries:
            blob += struct.pack("<4i", *e)
        blob += struct.pack("<i", len(samples))
        for smp in samples:
            blob += struct.pack("<7i", *smp)
    blob += hit_tables(t)
    blob += struct.pack("<9i", *t["winner"])
    for pointer, script in zip(t["shake_addresses"], t["shakes"]):
        blob += struct.pack("<I", pointer) + script
    prev = None
    for fighters, ram in frames(header, body):
        for k, rec in enumerate(fighters):
            blob += struct.pack("<10iIIIiiiii11i", *fighter_fields(rec, banks, k))
        before = prev if prev is not None else ram
        blob += struct.pack("<3i", ram_read(before, FIGHTER_DISTANCE, "i"),
                            ram_read(before, CAMERA_PHASE, "h"), ram_read(before, ROUNDS_PLAYED, "h"))
        for name, addr, length in RANGES:
            base, data = next((b, d) for b, d in ram.values() if b <= addr and addr + length <= b + len(d))
            blob += data[addr - base:addr - base + length]
        prev = ram
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"{header['frames']} frames -> {out} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
