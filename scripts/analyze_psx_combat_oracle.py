#!/usr/bin/env python3
"""Extract measured combat events from a private Tekken 3 PS1 JSONL capture.

Never infer hitstun, knockdown, combo policy or causal hit reaction without
a PlayStation source trace. The output is descriptive: HP deltas, move-slot
changes, KO flag edges and runs where pose frames stay constant.
"""
import argparse
import json
from pathlib import Path
import sys

FIELDS = (
    "state", "move_frame", "hit_freeze", "recover_frames",
    "got_hit", "guarded", "hit_clean", "counter_hit", "in_air",
    "root_x", "root_y", "root_z", "facing",
)
RAW_RANGES = {
    "state": (0, 0xFFFFFFFF),
    "move_frame": (-32768, 32767),
    "hit_freeze": (-32768, 32767),
    "recover_frames": (-32768, 32767),
    "got_hit": (0, 255), "guarded": (0, 255),
    "hit_clean": (0, 255), "counter_hit": (0, 255),
    "in_air": (0, 255),
    "root_x": (-0x80000000, 0x7FFFFFFF),
    "root_y": (-0x80000000, 0x7FFFFFFF),
    "root_z": (-0x80000000, 0x7FFFFFFF),
    "facing": (0, 65535),
}
TOP_RANGES = {
    "tick": (0, 0xFFFFFFFF), "fighter": (0, 1),
    "source_frame": (0, 0xFFFFFFFF), "move_slot": (0, 4022),
    "pose_frame": (0, 0xFFFF), "health_q16": (0, 0x7FFFFFFF),
    "ko": (0, 1),
}


class InvalidOracle(ValueError):
    pass


def _number(value, bounds, name, location):
    if type(value) is not int or not bounds[0] <= value <= bounds[1]:
        raise InvalidOracle(f"{location}: {name} must be a valid integer")


def read_capture(path):
    ticks = []
    pair = {}
    prev_frame = None
    previous_tick = None
    with Path(path).open("r", encoding="utf8") as stream:
        for n, line in enumerate(stream, 1):
            if not line.strip():
                raise InvalidOracle(f"{path}:{n}: empty row")
            if len(ticks) >= 60000:
                raise InvalidOracle(f"{path}: too many frames")
            try:
                row = json.loads(line)
            except json.JSONDecodeError as e:
                raise InvalidOracle(f"{path}:{n}: bad JSON: {e}") from e
            if not isinstance(row, dict) or not isinstance(row.get("psx"), dict):
                raise InvalidOracle(f"{path}:{n}: missing raw PS1 fighter object")
            where = f"{path}:{n}"
            for key, bounds in TOP_RANGES.items():
                _number(row.get(key), bounds, key, where)
            for key, bounds in RAW_RANGES.items():
                _number(row["psx"].get(key), bounds, f"psx.{key}", where)
            tick = row["tick"]
            player = row["fighter"]
            if previous_tick is None:
                if tick != 0:
                    raise InvalidOracle(f"{where}: first tick must be zero")
            elif tick != previous_tick and tick != previous_tick+1:
                raise InvalidOracle(f"{where}: nonconsecutive tick")
            if previous_tick is not None and tick != previous_tick:
                if set(pair) != {0, 1}:
                    raise InvalidOracle(f"{where}: incomplete fighter pair")
                if pair[0]["source_frame"] != pair[1]["source_frame"]:
                    raise InvalidOracle(f"{where}: inconsistent guest counters")
                frame = pair[0]["source_frame"]
                if prev_frame is not None and (frame - prev_frame) % (1 << 32) != 1:
                    raise InvalidOracle(f"{where}: guest frame gap")
                prev_frame = frame
                ticks.append(pair)
                pair = {}
            if player in pair:
                raise InvalidOracle(f"{where}: duplicate fighter in tick {tick}")
            pair[player] = row
            previous_tick = tick
    if set(pair) != {0, 1}:
        raise InvalidOracle(f"{path}: empty or incomplete final pair")
    if pair[0]["source_frame"] != pair[1]["source_frame"]:
        raise InvalidOracle(f"{path}: inconsistent final guest frame")
    frame = pair[0]["source_frame"]
    if prev_frame is not None and (frame - prev_frame) % (1 << 32) != 1:
        raise InvalidOracle(f"{path}: final guest frame gap")
    ticks.append(pair)
    return ticks


def analyze(ticks):
    events = []
    pose_static = [None, None]
    summary = {
        "frames": len(ticks),
        "first_source_frame": ticks[0][0]["source_frame"],
        "last_source_frame": ticks[-1][0]["source_frame"],
        "events": 0,
        "hp_changed": [0, 0],
        "moves_changed": [0, 0],
        "ko_edges": [0, 0],
        "pose_static_runs": [0, 0],
    }

    def emit(tick, player, kind, **payload):
        events.append({"tick": tick, "fighter": player,
                       "source_frame": ticks[tick][player]["source_frame"],
                       "kind": kind, **payload})

    for tick in range(1, len(ticks)):
        for player in (0, 1):
            prev, current = ticks[tick - 1][player], ticks[tick][player]
            if current["move_slot"] != prev["move_slot"]:
                emit(tick, player, "move_slot_changed",
                     before=prev["move_slot"], after=current["move_slot"],
                     pose_frame=current["pose_frame"])
                summary["moves_changed"][player] += 1
            if current["health_q16"] != prev["health_q16"]:
                delta = current["health_q16"] - prev["health_q16"]
                emit(tick, player, "health_changed",
                     before=prev["health_q16"], after=current["health_q16"],
                     delta_q16=delta)
                summary["hp_changed"][player] += 1
            if current["ko"] != prev["ko"]:
                emit(tick, player, "ko_flag_changed",
                     before=prev["ko"], after=current["ko"])
                summary["ko_edges"][player] += 1
            for name in ("got_hit", "guarded", "hit_clean",
                         "counter_hit", "in_air", "hit_freeze",
                         "recover_frames"):
                before = prev["psx"][name]
                after = current["psx"][name]
                if before != after:
                    emit(tick, player, "raw_field_changed",
                         field=name, before=before, after=after)
            still = (current["move_slot"] == prev["move_slot"] and
                     current["pose_frame"] == prev["pose_frame"])
            if still and pose_static[player] is None:
                pose_static[player] = tick - 1
            if not still and pose_static[player] is not None:
                start = pose_static[player]
                emit(tick, player, "pose_static_interval",
                     first_tick=start, last_tick=tick-1,
                     observations=tick-start)
                summary["pose_static_runs"][player] += 1
                pose_static[player] = None
    for player in (0, 1):
        if pose_static[player] is not None:
            start = pose_static[player]
            emit(len(ticks)-1, player, "pose_static_interval",
                 first_tick=start, last_tick=len(ticks)-1,
                 observations=len(ticks)-start, open_at_end=True)
            summary["pose_static_runs"][player] += 1
    events.sort(key=lambda event: (event["tick"], event["fighter"], event["kind"]))
    summary["events"] = len(events)
    return {"schema": "t3-ps1-observed-combat-v1",
            "summary": summary, "events": events,
            "interpretation": "raw changes only; no inferred stun, knockdown or combo durations"}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ps1", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args(argv)
    try:
        report = analyze(read_capture(args.ps1))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    except (OSError, InvalidOracle) as exc:
        print(f"Invalid PS1 oracle capture: {exc}", file=sys.stderr)
        return 2
    print(f"Observed {report['summary']['frames']} PS1 ticks; "
          f"{report['summary']['events']} raw field/pose/HP events")
    return 0


if __name__ == "__main__":
    sys.exit(main())
