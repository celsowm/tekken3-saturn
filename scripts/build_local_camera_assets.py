#!/usr/bin/env python3
"""Embed the PRIVATE fight camera tables into a local Saturn build.

Reads the USA executable through the release layout (psx_camera_tables.py)
and the fighter's raw DIVMOT bank (sections 7 and 8, which the logic-only
.t3logic banks drop), and writes t3_local_camera.{h,c}: the camera port's
EXE tables, the bank's camera choices and, with --with-streams, its camera
streams baked to per-frame samples. The trig, atan and square root tables
are not repeated here; the caller points the tables at the copies the build
already embeds. Never use the output in public CI or releases.
"""
import argparse
import struct
from pathlib import Path

from divmot_to_logic import sections_of
from psx_camera_tables import Exe, USA_EXE, USA_LAYOUT, spline_tables, streams, tables

CHOICE_SECTION, STREAM_SECTION = 7, 8


def array(values, per_line=12) -> str:
    values = [str(v) for v in values]
    return "".join("    " + ",".join(values[i:i + per_line]) + ",\n" for i in range(0, len(values), per_line))


def fnv1a(data: bytes) -> int:
    digest = 2166136261
    for byte in data:
        digest = ((digest ^ byte) * 16777619) & 0xffffffff
    return digest


def compact_streams(data: bytes, section8: int, choices: list) -> tuple[list, bytes]:
    """Copy just the referenced section-8 streams, retaining their raw words."""
    parser = __import__('psx_camera_tables').research('camera_scripts')
    entries, blob = [], bytearray()
    for _, _, script in choices:
        if script >= -1:
            continue
        offset = script & 0xffff
        if any(entry[0] == offset for entry in entries):
            continue
        at = section8 + offset
        mode, stream = parser.stream_at(data, at, None)
        size = 2 + 2 * stream.size
        if at + size > len(data):
            raise ValueError(f'camera stream {offset:#x} exceeds bank')
        entries.append((offset, stream.frames, mode, len(blob), size))
        blob.extend(data[at:at + size])
    return entries, bytes(blob)


def stream_definitions(prefix: str, entries: list, samples: list,
                       encoded: bytes, compact: bool, cold: bool) -> str:
    rows = ["{%du,%du,%du,%du,%du}" % e for e in entries]
    result = (f"static const t3_psx_camera_stream_t {prefix}_entries[{max(1,len(entries))}] = {{\n"
              + (array(rows, 4) if rows else "    {0u,0u,0u,0u,0u},\n") + "};\n")
    if compact:
        name = prefix + '_encoded'
        if cold:
            result += f"uint8_t {name}[{max(1,len(encoded))}] __attribute__((section(\".wram_l\")));\n"
        else:
            result += (f"static const uint8_t {name}[{max(1,len(encoded))}] = {{\n"
                       + (array([str(v) for v in encoded], 16) if encoded else "    0,\n") + "};\n")
        result += (f"const t3_psx_camera_streams_t {prefix}_bank_streams = {{\n"
                   f"    .streams={prefix}_entries,.stream_count={len(entries)}u,"
                   f".encoded={name},.encoded_size={len(encoded)}u\n}};\n")
    else:
        result += (f"static const int16_t {prefix}_samples[{max(1,len(samples))}][7] = {{\n"
                   + (array(["{" + ",".join(str(v) for v in s) + "}" for s in samples], 4)
                      if samples else "    {0},\n") + "};\n")
        result += (f"const t3_psx_camera_streams_t {prefix}_bank_streams = {{\n"
                   f"    .streams={prefix}_entries,.stream_count={len(entries)}u,"
                   f".samples={prefix}_samples,.sample_count={len(samples)}u\n}};\n")
    return result


def generate(bank_path: Path, spline_path: Path, out: Path, exe_path: Path, layout: Path,
             with_streams: bool, second_bank_path: Path | None = None,
             compact: bool = False, cold: bool = False, tables_only: bool = False):
    if with_streams and compact or cold and not compact:
        raise ValueError('use either sampled or compact streams; cold requires compact')
    t = tables(Exe.usa(exe_path, layout))
    bank = bank_path.read_bytes()
    _, sec = sections_of(bank)
    if bank[1] == 99:
        raise ValueError("the camera bank must be a character bank")
    choices = [struct.unpack_from("<hHi", bank, a) for a in range(sec[CHOICE_SECTION], sec[CHOICE_SECTION + 1] - 7, 8)]
    if tables_only:
        choices = []
    second_choices = []
    second_bank = None
    second_sec = None
    if second_bank_path:
        second_bank = second_bank_path.read_bytes()
        _, second_sec = sections_of(second_bank)
        if second_bank[1] == 99:
            raise ValueError("the second camera bank must be a character bank")
        second_choices = [struct.unpack_from("<hHi", second_bank, a)
                          for a in range(second_sec[CHOICE_SECTION], second_sec[CHOICE_SECTION + 1] - 7, 8)]
    entries, samples, encoded = [], [], b''
    second_entries, second_samples, second_encoded = [], [], b''
    if compact:
        entries, encoded = compact_streams(bank, sec[STREAM_SECTION], choices)
        if second_bank_path:
            second_entries, second_encoded = compact_streams(second_bank, second_sec[STREAM_SECTION], second_choices)
    elif with_streams:
        spline = spline_tables(spline_path.read_bytes())
        entries, samples = streams(bank, sec[STREAM_SECTION], choices, spline)
        entries = [(*entry, 0) for entry in entries]
        if second_bank_path:
            second_entries, second_samples = streams(second_bank, second_sec[STREAM_SECTION], second_choices, spline)
            second_entries = [(*entry, 0) for entry in second_entries]
    rows = t["hit_rows"]
    records = t["hit_lists"]
    if len(records) != 3 or any(len(r) > 4 for r in records):
        raise ValueError("hit camera lists exceed the port's 3 x 4 records")
    intro = t["intro_offsets"]
    presets = t["presets"]
    shakes = t["shakes"]
    cold_digest = fnv1a(encoded + second_encoded) if cold else 0

    def record(r):
        return "{%d,{%d,%d,%d,%d}}" % tuple(r)

    def script(s):
        return "{%d,%d,%d,%d,%d,%d,{%d,%d,%d,%d},{%d,%d,%d,%d}}" % tuple(s)

    def choice(c):
        return "{%d,%du,%d}" % c

    header = f"""#ifndef T3_LOCAL_CAMERA_H
#define T3_LOCAL_CAMERA_H
#include "psx_camera.h"
/* PRIVATE original camera tables (USA). trig, atan and sqrt are left NULL:
 * copy the tables and point them at the build's own copies. */
extern const t3_psx_camera_tables_t t3_local_camera_tables;
#define T3_LOCAL_CAMERA_BANK_CHOICES {len(choices)}u
extern const t3_psx_camera_choice_t t3_local_camera_bank_choices[{max(1, len(choices))}];
#define T3_LOCAL_CAMERA_STREAMS {len(entries)}u
extern const t3_psx_camera_streams_t t3_local_camera_bank_streams;
{f'#define T3_LOCAL_SECOND_CAMERA_BANK_CHOICES {len(second_choices)}u' if second_bank_path else ''}
{f'extern const t3_psx_camera_choice_t t3_local_second_camera_bank_choices[{max(1, len(second_choices))}];' if second_bank_path else ''}
{f'extern const t3_psx_camera_streams_t t3_local_second_camera_bank_streams;' if second_bank_path else ''}
{f'#define T3_LOCAL_COLD_CAMERA_BYTES {len(encoded)+len(second_encoded)}u' if cold else ''}
{f'#define T3_LOCAL_COLD_CAMERA_PRIMARY_BYTES {len(encoded)}u' if cold else ''}
{f'#define T3_LOCAL_COLD_CAMERA_SECOND_BYTES {len(second_encoded)}u' if cold and second_bank_path else ''}
{f'#define T3_LOCAL_COLD_CAMERA_FNV1A 0x{cold_digest:08X}u' if cold else ''}
{f'extern uint8_t t3_local_camera_encoded[{max(1,len(encoded))}];' if cold else ''}
{f'extern uint8_t t3_local_second_camera_encoded[{max(1,len(second_encoded))}];' if cold and second_bank_path else ''}
#endif
"""
    impl = ('#include "t3_local_camera.h"\n'
            "/* PRIVATE original camera data. Never publish derived objects/ISO. */\n"
            f"static const t3_psx_camera_choice_t exe_choices[{len(t['choices'])}] = {{\n"
            + array([choice(c) for c in t["choices"]], 6) + "};\n"
            f"const t3_psx_camera_choice_t t3_local_camera_bank_choices[{max(1, len(choices))}] = {{\n"
            + (array([choice(c) for c in choices], 6) if choices else "    {0,0u,0},\n") + "};\n"
            + (f"const t3_psx_camera_choice_t t3_local_second_camera_bank_choices[{max(1, len(second_choices))}] = {{\n"
               + (array([choice(c) for c in second_choices], 6) if second_choices else "    {0,0u,0},\n")
               + "};\n" if second_bank_path else "")
            +
            stream_definitions('t3_local_camera', entries, samples, encoded, compact, cold)
            + (stream_definitions('t3_local_second_camera', second_entries, second_samples,
                                  second_encoded, compact, cold) if second_bank_path else '')
            +
            "const t3_psx_camera_tables_t t3_local_camera_tables = {\n"
            "    .intro_offsets = {" + ",".join("{%d,%d,%d}" % tuple(intro[3 * i:3 * i + 3]) for i in range(10)) + "},\n"
            "    .presets = {\n" + array(["{" + ",".join(str(v) for v in presets[10 * i:10 * i + 10]) + "}" for i in range(30)], 2) + "    },\n"
            "    .preset_index = {" + ",".join(str(v) for v in t["preset_index"]) + "},\n"
            f"    .outside_blend_out = {t['outside_blend_out']},\n"
            f"    .choices = exe_choices, .choice_count = {len(t['choices'])}u,\n"
            "    .hit_rows = {" + ",".join("{%d,%d,%d}" % tuple(rows[3 * i:3 * i + 3]) for i in range(4)) + "},\n"
            "    .hit_records = {" + ",".join("{" + ",".join(record(r) for r in lst) + "}" for lst in records) + "},\n"
            "    .hit_record_count = {" + ",".join(str(len(lst)) for lst in records) + "},\n"
            "    .hit_scripts = {\n" + array([script(s) for s in t["hit_scripts"]], 1) + "    },\n"
            "    .winner_lateral = {%d,%d,%d}, .winner_forward = {%d,%d,%d}, .winner_height = {%d,%d,%d},\n" % tuple(t["winner"])
            + "    .shakes = {" + ",".join("{" + ",".join(str(struct.unpack("b", bytes([b]))[0]) for b in s) + "}" for s in shakes)
            + "},\n};\n")
    out.mkdir(parents=True, exist_ok=True)
    if cold:
        (out / 'CAMERA.BIN').write_bytes(encoded + second_encoded)
    (out / "t3_local_camera.h").write_text(header)
    (out / "t3_local_camera.c").write_text(impl)
    return len(choices), len(entries), len(samples)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--bank", type=Path, required=True, help="the fighter's RAW DIVMOT bank (BNS record)")
    p.add_argument("--second-bank", type=Path, help="P2's raw DIVMOT bank camera choices")
    p.add_argument("--spline-tables", type=Path, required=True, help="private 0x660-byte PS1 spline window")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--exe", type=Path, default=USA_EXE, help="the USA SLUS_004.02")
    p.add_argument("--layout", type=Path, default=USA_LAYOUT)
    p.add_argument("--with-streams", action="store_true",
                   help="embed the bank's camera streams (only throws start them)")
    p.add_argument("--compact-streams", action="store_true",
                   help="retain original compact stream words instead of sampled frames")
    p.add_argument("--cold-streams", action="store_true",
                   help="write CAMERA.BIN and Low WRAM buffers for compact streams")
    p.add_argument("--tables-only", action="store_true",
                   help="only the EXE tables: each fighter's choices and streams come from its pack")
    a = p.parse_args()
    try:
        counts = generate(a.bank, a.spline_tables, a.output, a.exe, a.layout,
                          a.with_streams, a.second_bank, a.compact_streams, a.cold_streams, a.tables_only)
    except (OSError, ValueError) as e:
        p.exit(1, f"private camera embed failed: {e}\n")
    print("camera tables: %d bank choices, %d streams, %d samples" % counts)


if __name__ == "__main__":
    main()
