#!/usr/bin/env python3
"""Read the fight camera's EXE tables from the Japan Rev.1 or the USA executable.

Addresses are Japan Rev.1 (the research docs' and camera port's). The USA
executable is read through the pinned Tekken3Remake layout
(`tools/remake_import/layouts/usa.json`): each table must lie inside one
segment the layout copies unchanged, and pointers inside the tables are
mapped back to Rev.1 addresses. The tables are retail data: anything this
module returns stays private (git-ignored under build/).

    scripts/psx_camera_tables.py --compare   # USA tables equal Japan Rev.1's
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REMAKE = ROOT / "third_party" / "Tekken3Remake"
JP_EXE = REMAKE / "work" / "jp_rev1" / "exe.bin"
USA_EXE = ROOT / "assets" / "imported" / "tekken3-usa" / "SLUS_004.02"
USA_LAYOUT = REMAKE / "tools" / "remake_import" / "layouts" / "usa.json"
BASE, HEADER = 0x80010000, 0x800

INTRO_OFFSETS = 0x800988E8
SHAKE_SCRIPTS = 0x80097EC8  # 3 pointers to s8 shake scripts ending at -128
SHAKE_BYTES = 96
WINNER_TABLES = 0x8001DF54  # lateral, forward, height: 3 s16 each, 8 bytes apart
CAMERA_PRESETS, CAMERA_PRESET_INDEX, CAMERA_OUTSIDE_BLEND_OUT = 0x80024184, 0x800243DC, 0x80024172
CAMERA_CHOICES, CAMERA_CHOICE_COUNT = 0x80098750, 0x25
HIT_CAMERA_ROWS, HIT_CAMERA_LISTS, HIT_CAMERA_SCRIPTS = 0x80024510, 0x800244F8, 0x80024418
HIT_CAMERA_LIST_COUNT, HIT_CAMERA_SCRIPT_COUNT = 3, 7


class Exe:
    """An executable read by Japan Rev.1 address."""

    def __init__(self, data: bytes, segments=None):
        self.data, self.segments = data, segments

    @classmethod
    def jp(cls, path: Path = JP_EXE) -> "Exe":
        return cls(path.read_bytes())

    @classmethod
    def usa(cls, path: Path = USA_EXE, layout: Path = USA_LAYOUT) -> "Exe":
        data = path.read_bytes()
        block = json.loads(layout.read_text())["blocks"]["exe"]
        if hashlib.sha256(data[HEADER:]).hexdigest() != block["sha256"]:
            raise ValueError("the USA executable does not match the release layout")
        return cls(data, block["segments"])

    def offset(self, addr: int, size: int) -> int:
        off = addr - BASE
        if self.segments is None:
            return off
        found = [s + off - c for c, n, s in self.segments if c <= off and off + size <= c + n]
        if len(found) != 1:
            raise ValueError(f"{addr:#x}+{size}: not inside one layout segment")
        return found[0]

    def read(self, addr: int, size: int) -> bytes:
        at = HEADER + self.offset(addr, size)
        return self.data[at:at + size]

    def unpack(self, fmt: str, addr: int):
        return struct.unpack("<" + fmt, self.read(addr, struct.calcsize("<" + fmt)))

    def s16(self, addr: int, count: int):
        return self.unpack(f"{count}h", addr)

    def pointer(self, value: int, size: int = 1) -> int:
        """The Rev.1 address of a pointer value read from this executable."""
        if self.segments is None:
            return value
        off = value - BASE
        found = [c + off - s for c, n, s in self.segments if s <= off and off + size <= s + n]
        if len(found) != 1:
            raise ValueError(f"pointer {value:#x}: not inside one layout segment")
        return BASE + found[0]


def shake_script(exe: Exe, addr: int) -> bytes:
    """One shake script up to its -128 end, padded with -128 to SHAKE_BYTES."""
    out = bytearray()
    while not out or out[-1] != 0x80:
        if len(out) == SHAKE_BYTES:
            raise ValueError(f"shake script {addr:#x} has no end")
        out += exe.read(addr + len(out), 1)
    return bytes(out.ljust(SHAKE_BYTES, b"\x80"))


def hit_tables(exe: Exe):
    """Rows [4][3]; per list the records (cumulative threshold out of 0x1000, as
    FUN_80067ECC makes them at the first fight, and 4 script indices); the
    scripts [7] x 14 (kind, frames, blend in/out, distances, pitches, yaws)."""
    rows = exe.s16(HIT_CAMERA_ROWS, 12)
    lists = []
    for k in range(HIT_CAMERA_LIST_COUNT):
        pointer, count = exe.unpack("II", HIT_CAMERA_LISTS + 8 * k)
        pointer = exe.pointer(pointer, 20 * count)
        records = []
        for j in range(count):
            weight = exe.unpack("h", pointer + 20 * j)[0]
            scripts = [(exe.pointer(p, 32) - HIT_CAMERA_SCRIPTS) // 32 for p in exe.unpack("4I", pointer + 20 * j + 4)]
            records.append([weight, *scripts])
        total, cumulative = sum(r[0] for r in records), 0
        for r in records:
            cumulative += r[0]
            r[0] = int((cumulative << 12) / total)
        lists.append(records)
    scripts = [exe.unpack("4h2i8h", HIT_CAMERA_SCRIPTS + 32 * k) for k in range(HIT_CAMERA_SCRIPT_COUNT)]
    return rows, lists, scripts


def spline_tables(blob: bytes):
    """The research port's B-spline tables from the 0x660-byte window
    (0x80096B00 - 0x40) that the private pose tables already hold."""
    motion = research("motion")
    if len(blob) != 0x660:
        raise ValueError("the spline window must be 0x660 bytes")
    tab = motion.SplineTables.__new__(motion.SplineTables)
    tab.base, tab.blob = motion.SPLINE_TABLE_BASE - 0x40, blob
    return tab


def research(name: str):
    import importlib
    import sys
    path = str(REMAKE / "tools" / "research")
    if path not in sys.path:
        sys.path.insert(0, path)
    return importlib.import_module(name)


def streams(data: bytes, section8: int, choices, tab) -> tuple[list, list]:
    """Every camera stream the choices name (scripts < -1, at `section8 +
    (script & 0xFFFF)` in `data`), sampled per frame by the research decoder
    camera_scripts.py: entries (offset, frames, yaw mode, first sample) and
    samples (eye xyz, target xyz, weight)."""
    camera_scripts = research("camera_scripts")
    entries, samples = [], []
    for _kind, _weight, script in choices:
        offset = script & 0xFFFF
        if script >= -1 or any(e[0] == offset for e in entries):
            continue
        mode, stream = camera_scripts.stream_at(data, section8 + offset, tab)
        entries.append((offset, stream.frames, mode, len(samples)))
        samples += [stream.sample(f) for f in range(stream.frames)]
    return entries, samples


def tables(exe: Exe) -> dict:
    """Every EXE table of the camera port, by name."""
    rows, lists, scripts = hit_tables(exe)
    shakes = [exe.pointer(p) for p in exe.unpack("3I", SHAKE_SCRIPTS)]
    return dict(
        intro_offsets=exe.s16(INTRO_OFFSETS, 30),
        presets=exe.s16(CAMERA_PRESETS, 300),
        preset_index=exe.s16(CAMERA_PRESET_INDEX, 30),
        outside_blend_out=exe.s16(CAMERA_OUTSIDE_BLEND_OUT, 1)[0],
        choices=[exe.unpack("hHi", CAMERA_CHOICES + 8 * k) for k in range(CAMERA_CHOICE_COUNT)],
        hit_rows=rows, hit_lists=lists, hit_scripts=scripts,
        winner=[v for k in range(3) for v in exe.s16(WINNER_TABLES + 8 * k, 3)],
        shake_addresses=shakes,
        shakes=[shake_script(exe, a) for a in shakes])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--compare", action="store_true", help="check the USA tables against Japan Rev.1's")
    a = p.parse_args()
    if not a.compare:
        p.error("nothing to do")
    jp, usa = tables(Exe.jp()), tables(Exe.usa())
    differ = [k for k in jp if jp[k] != usa[k]]
    for k in differ:
        print(f"differs: {k}")
    print(f"{len(jp) - len(differ)} of {len(jp)} camera tables equal")
    return 1 if differ else 0


if __name__ == "__main__":
    raise SystemExit(main())
