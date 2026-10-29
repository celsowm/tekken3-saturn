#!/usr/bin/env python3
"""Measure affine versus perspective UVs in a captured original floor grid.

Reads private game RAM; prints aggregate numbers only. The geometry and
triangle order follow the pinned Tekken3Remake stages.md research.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
from fight_oracle_export import Layout

ROOT = Path(__file__).resolve().parents[1]
USA_EXE_SHA256 = 'fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c'


def centroid_error(uv, depth, triangle):
    affine = [sum(uv[j][axis] for j in triangle) / 3 for axis in (0, 1)]
    denominator = sum(1 / depth[j] for j in triangle)
    corrected = [sum(uv[j][axis] / depth[j] for j in triangle) / denominator for axis in (0, 1)]
    return math.dist(affine, corrected)


def additive_shading_error(stage, levels):
    """Best constant RGB offsets versus multiplicative 5-bit color, per level.

    This is an optimistic palette-weighted model, not a VDP1 raster capture:
    each level may independently choose the best correction for each channel.
    """
    data = stage.read_bytes()
    if len(data) < 4128 or data[:8] != b'T3ST\x00\x00\x00\x02':
        raise ValueError('expected version 2 PAUL.STG')
    palette = struct.unpack_from('>16H', data, 4096)
    counts = [0] * 16
    for byte in data[2048:4096]:
        counts[byte >> 4] += 1
        counts[byte & 15] += 1
    total = len(levels) * sum(counts) * 3
    baseline_error = best_error = 0
    for level in levels:
        for shift in (0, 5, 10):
            values = [(color >> shift) & 31 for color in palette]
            targets = [min(31, int(value * level / 128 + 0.5)) for value in values]
            def error(delta):
                return sum(count * abs(max(0, min(31, value + delta)) - target)
                           for count, value, target in zip(counts, values, targets))
            baseline_error += error(0)
            best_error += min(error(delta) for delta in range(-16, 16))
    return {'sampled_centroid_levels': len(levels),
            'baseline_mean_absolute_error_5bit': round(baseline_error / total, 4),
            'best_additive_mean_absolute_error_5bit': round(best_error / total, 4),
            'model': 'independent optimal RGB offsets per centroid, weighted by whole tile palette'}


def original_sqrt(n, table):
    if n == 0:
        return 0
    if n > 0x7fffffff or len(table) < 384:
        raise ValueError('SquareRoot0 input or table outside supported range')
    leading = (32 - n.bit_length()) & ~1
    shift = 24 - leading
    index = n >> shift if shift >= 0 else n << -shift
    coefficient = struct.unpack_from('<h', table, 2 * (index - 64))[0]
    return (coefficient << ((31 - leading) >> 1)) >> 12


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ram', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--stage', type=Path, help='optional private generated PAUL.STG')
    parser.add_argument('--sqrt', type=Path, help='optional private PS1 SquareRoot0 table')
    parser.add_argument('--projection-eval', type=Path, help='optional compiled floor projection evaluator')
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != USA_EXE_SHA256:
        raise ValueError('unexpected USA executable')
    ram = args.ram.read_bytes()
    if len(ram) != 2 * 1024 * 1024:
        raise ValueError('expected 2 MiB PS1 RAM')
    layout = Layout(ROOT / 'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')

    def read(address, fmt):
        offset = (address & 0x1fffff)
        if offset + struct.calcsize('<' + fmt) > len(ram):
            raise ValueError('address outside captured RAM')
        return struct.unpack_from('<' + fmt, ram, offset)[0]

    def original(address):
        return layout.to_usa(address)

    ptr = read(original(0x800AE1DC), 'I')
    if not 0x80000000 <= ptr < 0x80200000:
        raise ValueError('floor buffer pointer outside PS1 RAM')
    tile_size = read(original(0x8009EB28), 'I')
    cols, rows = (read(original(a), 'I') for a in (0x8009EBCC, 0x8009EBD0))
    if (tile_size, cols, rows) != (1800, 10, 10):
        raise ValueError('expected standard 10x10 floor')
    matrix_base = original(0x800AE2E8)
    matrix = [read(matrix_base + 2 * n, 'h') for n in range(9)]
    translation = [read(matrix_base + 20 + 4 * n, 'i') for n in range(3)]
    focal = read(original(0x800A919C), 'H')
    if focal <= 0:
        raise ValueError('invalid focal distance')
    errors, uncut, skipped, sampled_levels = [], 0, 0, []
    projected_differences = []
    if args.projection_eval:
        # This capture's grid centre is zero; reject other setups explicitly.
        roots = [struct.unpack_from('<i', ram, address & 0x1fffff)[0]
                 for address in (0x800a9228, 0x800a9230, 0x800aaab4, 0x800aaabc)]
        if roots[0]+roots[2] or roots[1]+roots[3]:
            raise ValueError('projection audit requires a zero midpoint')
        values = matrix + translation + [focal, 0, 0]
        execution = subprocess.run([str(args.projection_eval)], input=' '.join(map(str, values))+'\n',
                                   text=True, capture_output=True, check=True)
        grid = [tuple(map(int, line.split())) for line in execution.stdout.splitlines()]
        if len(grid)!=121 or any(len(vertex)!=3 for vertex in grid):
            raise ValueError('invalid projection evaluator output')
    brightness = {'near': [], 'far': []}
    shade_differences = []
    if args.sqrt:
        if not args.stage:
            raise ValueError('--sqrt requires --stage')
        def read_usa(address, fmt):
            return struct.unpack_from('<' + fmt, ram, address & 0x1fffff)[0]
        eye_x = read_usa(0x800a85d4, 'i')
        eye_z = read_usa(0x800a85dc, 'i')
        def half_toward_zero(n):
            return n // 2 if n >= 0 else -((-n) // 2)
        midpoint_x = half_toward_zero(sum(read_usa(a, 'i') for a in (0x800a9228, 0x800aaab4)))
        midpoint_z = half_toward_zero(sum(read_usa(a, 'i') for a in (0x800a9230, 0x800aaabc)))
        camera_distance = original_sqrt((eye_x-midpoint_x)**2 + (eye_z-midpoint_z)**2,
                                        args.sqrt.read_bytes())
        fade = args.stage.read_bytes()[4128:5152]
        if len(fade) != 1024:
            raise ValueError('truncated stage fade table')
    # Buffer 0 at this observed Vsync; screening against the current GTE view
    # discards clipped or stale packets instead of treating them as exact.
    for index in range(100):
        base = ptr + 0x5e4 + index * 52
        words = [read(base + 4 * j, 'I') for j in range(13)]
        uv_words = [words[j] & 0xffff for j in (3, 6, 9, 12)]
        if uv_words != [0x40, 0x7f, 0x3f40, 0x3f7f]:
            continue
        xy = [struct.unpack('<hh', struct.pack('<I', words[j])) for j in (2, 5, 8, 11)]
        uv = [(value & 255, value >> 8) for value in uv_words]
        levels = [words[j] & 255 for j in (1, 4, 7, 10)]
        if any(((words[j] >> 8) & 255) != levels[k] or ((words[j] >> 16) & 255) != levels[k]
               for k, j in enumerate((1, 4, 7, 10))):
            raise ValueError('floor vertex colors are not grayscale')
        x0, z0 = -9000 + 1800 * (index % 10), -9000 + 1800 * (index // 10)
        world = [(x0, z0 + 1800), (x0 + 1800, z0 + 1800), (x0, z0), (x0 + 1800, z0)]
        depth = []
        valid = True
        for j, (x, z) in enumerate(world):
            cx = (matrix[0] * x + matrix[2] * z) / 4096 + translation[0]
            cy = (matrix[3] * x + matrix[5] * z) / 4096 + translation[1]
            cz = (matrix[6] * x + matrix[8] * z) / 4096 + translation[2]
            if cz <= 0:
                valid = False
                break
            depth.append(cz)
            projected = (192 + cx * focal / cz, 240 + cy * focal / cz)
            if max(abs(xy[j][axis] - projected[axis]) for axis in (0, 1)) > 2:
                valid = False
                break
        if not valid:
            skipped += 1
            continue
        uncut += 1
        if args.projection_eval:
            x, z = index % 10, index // 10
            ids = ((z+1)*11+x, (z+1)*11+x+1, z*11+x, z*11+x+1)
            projected_differences.extend((grid[ids[j]][0]-xy[j][0], grid[ids[j]][1]-xy[j][1])
                                         for j in range(4))
        if args.sqrt:
            for j, (x, z) in enumerate(world):
                sz = max(0, min(65535, (matrix[6] * x + matrix[8] * z) // 4096 + translation[2]))
                index = min(1023, max(0, sz-camera_distance) * (0x4000000 // 10000) >> 16)
                predicted = 128 - (fade[index] * 51 + 127) // 128
                shade_differences.append(predicted - levels[j])
        for triangle in ((0, 1, 2), (1, 2, 3)):
            center_x = sum(xy[j][0] for j in triangle) / 3
            center_y = sum(xy[j][1] for j in triangle) / 3
            if 0 <= center_x < 368 and 20 <= center_y < 468:
                errors.append(centroid_error(uv, depth, triangle))
                sampled_levels.append(sum(levels[j] for j in triangle) / 3)
                bucket = 'near' if (center_y - 20) / 2 >= 175 else 'far'
                brightness[bucket].append(sum(levels[j] for j in triangle) / 3)
    if not errors:
        raise ValueError('no validated on-screen floor triangles')
    result = {'ram_sha256': hashlib.sha256(ram).hexdigest(), 'buffer': 0,
                      'unclipped_current_quads': uncut, 'rejected_clipped_or_stale': skipped,
                      'onscreen_centroids': len(errors), 'mean_uv_distance_texels': round(sum(errors)/len(errors), 4),
                      'maximum_uv_distance_texels': round(max(errors), 4),
                      'centroids_over_one_texel': sum(e > 1 for e in errors),
                      'centroids_over_two_texels': sum(e > 2 for e in errors),
                      'centroid_brightness': {name: {'count': len(values),
                          'mean': round(sum(values)/len(values), 2),
                          'minimum': round(min(values), 2), 'maximum': round(max(values), 2)}
                          for name, values in brightness.items() if values}}
    if args.stage:
        result['additive_shading_model'] = additive_shading_error(args.stage, sampled_levels)
    if args.sqrt:
        result['original_vertex_shade'] = {'camera_distance': camera_distance,
            'vertices': len(shade_differences),
            'exact': sum(x == 0 for x in shade_differences),
            'max_absolute_error': max(map(abs, shade_differences))}
    if args.projection_eval:
        result['floor_projection'] = {'vertices': len(projected_differences),
            'exact_xy': sum(pair == (0, 0) for pair in projected_differences),
            'max_absolute_error': max(max(map(abs, pair)) for pair in projected_differences),
            'differences': {f'{dx},{dy}': projected_differences.count((dx,dy))
                            for dx,dy in sorted(set(projected_differences))}}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
