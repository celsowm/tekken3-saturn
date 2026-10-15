#!/usr/bin/env python3
"""Generate original, independently redistributable animated fighter content.

The upstream LibSaturn generator is a host BUILD TOOL, not a runtime link
dependency. We derive only its original 'low' 24-joint animated rig and pack
one SKA1 archive for two independent actor instances. No commercial game
assets or ROM data are accessed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def generate(library_source: Path, output: Path) -> dict:
    tools = (library_source / "tools").resolve()
    if not (tools / "generate_character_examples.py").is_file():
        raise ValueError(
            f"LibSaturn source tools missing: {tools}/generate_character_examples.py"
        )
    sys.path.insert(0, str(tools))
    try:
        import generate_character_examples as gen
        from model_pipeline import skeletal_export as sx
        from model_pipeline.skeletal_binary import encode
        model, quads, materials = gen.build_source("low")
        export = sx.export_model(
            model, rate=gen.SAMPLE_RATE, loop_clips=gen.LOOP_CLIPS
        )
        render = gen.render_asset(model, export, quads, materials)
        events = {
            name: [(round(gen.SAMPLE_RATE * duration * .45), 1)]
            for name, duration in zip(gen.CLIP_NAMES, gen.CLIP_SECONDS)
            if name in ("punch", "kick")
        }
        roots = {name: 2 | 8 for name in ("walk", "run")}
        blob = encode(
            export, render, [entry[0] for entry in gen.JOINTS],
            events, roots
        )
        red_palette = gen.palette(True)
        clips = list(gen.CLIP_NAMES)
        vertex_count = len(export.skin.source_vertex)
        face_count = len(quads)
        joint_count = len(gen.JOINTS)
        texture_count = len(render.textures)
    finally:
        sys.path.remove(str(tools))

    if not 0 < len(blob) < 300000:
        raise ValueError(f"SKA1 archive is larger than the allowed 300 KB: {len(blob)}")
    if vertex_count > 2048 or face_count > 2048 or joint_count > 64:
        raise ValueError("Source geometry exceeds the explicit runtime storage budget")
    if len(clips) != 9 or texture_count > 64 or texture_count < 1:
        raise ValueError("Unexpected fighter generator clip/texture topology")
    if len(red_palette) != 256:
        raise ValueError("Expected one 256-color red alternate palette")
    output.mkdir(parents=True, exist_ok=True)

    enum_lines = [f"    T3_CLIP_{clip.upper()} = {index},"
                  for index, clip in enumerate(clips)]
    header = """#ifndef T3_GENERATED_CHARACTER_H
#define T3_GENERATED_CHARACTER_H

#include <stdint.h>
#define T3_ARCHIVE_BYTES %du
#define T3_VERTEX_COUNT %du
#define T3_FACE_COUNT %du
#define T3_JOINT_COUNT %du
#define T3_TEXTURE_COUNT %du
#define T3_CLIP_COUNT %du
enum {
%s
};
typedef union t3_embedded_archive {
    uint32_t alignment;
    uint8_t bytes[T3_ARCHIVE_BYTES];
} t3_embedded_archive_t;
extern t3_embedded_archive_t t3_archive;
extern const uint16_t t3_red_palette[256];
#endif
""" % (
        len(blob), vertex_count, face_count, joint_count,
        texture_count, len(clips), "\n".join(enum_lines)
    )
    code = ['#include "t3_generated_character.h"',
            "/* Mutable aligned SKA1 storage: decode edits endianness in-place. */",
            "t3_embedded_archive_t t3_archive = { .bytes = {"]
    for offset in range(0, len(blob), 24):
        code.append("    " + ", ".join(str(v) for v in blob[offset:offset+24]) + ",")
    code += ["} };", "const uint16_t t3_red_palette[256] = {"]
    for offset in range(0, 256, 16):
        code.append("    " + ", ".join(str(v) for v in red_palette[offset:offset+16]) + ",")
    code.append("};")
    (output / "t3_generated_character.h").write_text(header, encoding="utf-8")
    (output / "t3_generated_character.c").write_text(
        "\n".join(code) + "\n", encoding="utf-8"
    )
    manifest = {
        "asset": "Original LibSaturn low-LOD humanoid, procedural",
        "archive_bytes": len(blob),
        "archive_sha256": hashlib.sha256(blob).hexdigest(),
        "vertices": vertex_count,
        "faces": face_count,
        "joints": joint_count,
        "textures": texture_count,
        "clips": clips,
        "palette_alternates": 2,
        "format": "SKA1",
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--libsaturn-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = generate(args.libsaturn_source, args.output)
    except (ImportError, OSError, ValueError, AssertionError) as exc:
        parser.exit(1, f"fighter asset build failed: {exc}\n")
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
