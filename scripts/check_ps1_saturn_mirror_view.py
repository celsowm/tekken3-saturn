#!/usr/bin/env python3
"""Compare selected RAM and camera fields from PS1 and Saturn mirror captures."""
import argparse
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ps1_ram', type=Path)
    parser.add_argument('saturn_capture', type=Path)
    args = parser.parse_args()
    ram = args.ps1_ram.read_bytes()
    if len(ram) != 2 * 1024 * 1024:
        raise ValueError('expected a 2 MiB PS1 RAM dump')

    def read(address, fmt):
        return struct.unpack_from('<' + fmt, ram, address - 0x80000000)[0]

    observation = json.loads((args.saturn_capture / 'observation.json').read_text())
    state = observation['state']
    if state['first_error'] or observation['optimizations']['slave_sim_pose_tick'] != state['sim_ticks']:
        raise ValueError('guest error or pose snapshot differs from reported tick')
    roots, slots, frames, facing = [], [], [], []
    for base in (0x800A9228, 0x800AAAB4):
        roots.extend(read(base + offset, 'i') for offset in (0, 4, 8))
        slots.append(read(base + 0xA0, 'h'))
        frames.append(read(base + 0x58, 'h'))
        facing.append(read(base + 0x0E, 'H'))
    if roots != state['psx_anchor'] or slots != state['psx_move_slot'] or frames != state['psx_move_frame']:
        raise ValueError(f'fighter state mismatch: roots={roots} slots={slots} frames={frames}')
    if facing != [0x4000, 0xBFFF]:
        raise ValueError(f'unexpected original facing: {facing}')
    manifest = json.loads((args.saturn_capture / 'symbol-dumps.json').read_text())
    camera = (args.saturn_capture / manifest['_g_previous_camera']['file']).read_bytes()
    if len(camera) != 116:
        raise ValueError('unexpected Saturn camera structure')
    native = struct.unpack('>29i', camera)
    position = [read(0x800A85C0 + 0x14 + 4 * i, 'i') for i in range(3)]
    expected_eye = [position[0] * 96, -position[1] * 96, -position[2] * 96]
    if list(native[:3]) != expected_eye:
        raise ValueError(f'camera eye mismatch: {position} != {native[:3]}')
    matrix = [read(0x800ADE20 + 2 * i, 'h') for i in range(9)]
    expected_target = [expected_eye[0] + matrix[6] * 64,
                       expected_eye[1] - matrix[7] * 64,
                       expected_eye[2] - matrix[8] * 64]
    expected_up = [-matrix[3] * 16, matrix[4] * 16, matrix[5] * 16]
    if list(native[3:6]) != expected_target or list(native[6:9]) != expected_up:
        raise ValueError('camera axes differ')
    h = read(0x800A8CD4, 'H')
    if h != 500:
        raise ValueError(f'unexpected original projection H: {h}')
    print(json.dumps({'ps1_global_tick': read(0x800AFA4C, 'I'),
                      'saturn_local_tick': state['sim_ticks'], 'roots': roots,
                      'move_slots': slots, 'pose_frames': frames,
                      'original_eye': position, 'camera_matrix': matrix,
                      'ps1_projection_h': h, 'saturn_elf_sha256': observation['elf_sha256']}, indent=2))


if __name__ == '__main__':
    main()
