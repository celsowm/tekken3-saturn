#!/usr/bin/env python3
"""Decode unmodified Tekken 3 USA PS1 RAM snapshots into paired combat JSONL.

Read-only offline companion to pcsx_redux_capture_usa.lua. This decoder never
boots a disc and never pretends to have captured an original emulator run.
Input files are *full, consecutive 2 MiB emulated PS1 RAM snapshots* saved
by the user (e.g. PCSX-Redux /api/v1/cpu/ram/raw while single-stepping).
Addresses are from the upstream USA release layout, not the JP executable.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tempfile

RAM_BYTES = 2 * 1024 * 1024
FIGHTER_BASE = 0x800A9228  # USA, from verified JP->USA layout point
FIGHTER_STRIDE = 0x188C
GAME_STATE = 0x800AE204  # USA FightMain = state 8
GLOBAL_FRAME = 0x800AFA4C  # USA g_frameCounter
FIELDS = {
    "move_frame": (0x5C, "h"),
    "hit_freeze": (0x76, "h"),
    "recover_frames": (0x11A, "h"),
    "state": (0x60, "I"),
    "got_hit": (0xCE, "B"),
    "guarded": (0xCF, "B"),
    "hit_clean": (0xD0, "B"),
    "counter_hit": (0xD3, "B"),
    "in_air": (0xDB, "B"),
    "root_x": (0x00, "i"),
    "root_y": (0x04, "i"),
    "root_z": (0x08, "i"),
    "facing": (0x0E, "H"),
}


class InvalidSnapshot(ValueError):
    pass


def read_at(ram: bytes, address: int, fmt: str) -> int:
    off = address - 0x80000000
    if not 0 <= off <= RAM_BYTES - struct.calcsize(fmt):
        raise InvalidSnapshot(f"address {address:#x} outside 2 MiB RAM")
    return struct.unpack_from("<" + fmt, ram, off)[0]


def decode_snapshot(ram: bytes, tick: int) -> tuple[int, list[dict]]:
    if len(ram) != RAM_BYTES:
        raise InvalidSnapshot(f"RAM must be exactly {RAM_BYTES} bytes, got {len(ram)}")
    if not 0 <= tick <= 0xFFFFFFFF:
        raise InvalidSnapshot("tick outside unsigned 32-bit range")
    state = read_at(ram, GAME_STATE, "I")
    if state != 8:
        raise InvalidSnapshot(f"not FightMain (gameState={state}, expected 8)")
    source_frame = read_at(ram, GLOBAL_FRAME, "I")
    rows = []
    for player in range(2):
        base = FIGHTER_BASE + FIGHTER_STRIDE * player
        hp = read_at(ram, base + 0x3F4, "i")
        max_hp = read_at(ram, base + 0x3F8, "i")
        move = read_at(ram, base + 0x1A0, "h")
        pose = read_at(ram, base + 0x58, "h")
        ko = read_at(ram, base + 0xDE, "B")
        if not 0 < max_hp or not 0 <= hp <= max_hp:
            raise InvalidSnapshot(f"P{player+1} invalid health/max-health")
        if not 0 <= move < 4023 or not 0 <= pose <= 65535 or ko > 1:
            raise InvalidSnapshot(f"P{player+1} invalid move/pose/KO")
        raw = {name: read_at(ram, base + off, fmt)
               for name, (off, fmt) in FIELDS.items()}
        rows.append({
            "tick": tick, "fighter": player, "move_slot": move,
            "pose_frame": pose, "health_q16": hp, "ko": ko,
            "source_frame": source_frame, "psx": raw,
        })
    return source_frame, rows


def decode_files(paths: list[Path]):
    if not paths:
        raise InvalidSnapshot("at least one consecutive RAM dump is required")
    if len(paths) > 60000:
        raise InvalidSnapshot("too many frames (maximum 60000)")
    last_frame = None
    for tick, path in enumerate(paths):
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise InvalidSnapshot(f"cannot read {path}: {exc}") from exc
        try:
            frame, rows = decode_snapshot(data, tick)
        except InvalidSnapshot as exc:
            raise InvalidSnapshot(f"{path}: {exc}") from exc
        if last_frame is not None and (frame - last_frame) % (1 << 32) != 1:
            raise InvalidSnapshot(
                f"{path}: non-consecutive guest frame: {last_frame} -> {frame}")
        last_frame = frame
        yield rows


def write_trace(paths: list[Path], output: Path):
    # Validate *every* frame before publishing output (no partial oracle).
    frames = list(decode_files(paths))
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=output.parent,
            prefix=".psx-combat-", suffix=".tmp",
            delete=False,
        ) as stream:
            temp = Path(stream.name)
            for frame in frames:
                for row in frame:
                    stream.write(json.dumps(row, separators=(",", ":")) + "\n")
        os.replace(temp, output)
    finally:
        if temp is not None and temp.exists():
            temp.unlink()
    return len(frames)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True,
                   help="private JSONL destination (not checked into Git)")
    p.add_argument("ram", metavar="RAM.bin", type=Path, nargs="+",
                   help="consecutive 2 MiB RAM snapshots, sorted by filename")
    args = p.parse_args(argv)
    try:
        count = write_trace(sorted(args.ram), args.output)
    except (InvalidSnapshot, OSError) as exc:
        print(f"Invalid original-PS1 RAM capture: {exc}", file=sys.stderr)
        return 2
    print(f"Decoded {count} consecutive US PS1 frames / 2 fighters to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
