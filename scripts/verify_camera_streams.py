#!/usr/bin/env python3
"""Compare every compact camera stream sample with pinned original-code Python.

Compiles the production C decoder into a temporary host library. All retail
bank and spline data remains in private imported paths; no derived data is
written into the repository.
"""
import argparse
import ctypes
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from divmot_to_logic import sections_of
from psx_camera_tables import research

ROOT = Path(__file__).resolve().parents[1]


class Spline(ctypes.Structure):
    _fields_ = [('blob', ctypes.POINTER(ctypes.c_uint8)), ('length', ctypes.c_size_t)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('banks', type=Path, nargs='+')
    parser.add_argument('--spline', type=Path, default=ROOT / 'assets/imported/tekken3-usa/pose-tables/psx_spline.bin')
    parser.add_argument('--saturn-include', type=Path,
                        default=Path(os.environ.get('LIBSATURN_PREFIX', ROOT.parent / 'saturn-prefix')) / 'include')
    args = parser.parse_args()
    reference = research('camera_scripts')
    spline_data = args.spline.read_bytes()
    if len(spline_data) != 0x660:
        parser.error('spline blob must be 0x660 bytes')
    tab = reference.motion.SplineTables.__new__(reference.motion.SplineTables)
    tab.base = reference.motion.SPLINE_TABLE_BASE - 0x40
    tab.blob = spline_data
    spline_array = (ctypes.c_uint8 * len(spline_data)).from_buffer_copy(spline_data)
    spline = Spline(spline_array, len(spline_data))
    with tempfile.TemporaryDirectory(prefix='t3-camera-verify-') as tmp:
        library = Path(tmp) / ('camera.dylib' if sys.platform == 'darwin' else 'camera.so')
        subprocess.run(['cc', '-std=c11', '-O2', '-dynamiclib' if sys.platform == 'darwin' else '-shared',
                        '-fPIC', '-o', str(library),
                        '-I' + str(ROOT / 'src/game'),
                        '-I' + str(args.saturn_include),
                        str(ROOT / 'src/game/psx_camera.c'),
                        str(ROOT / 'src/game/psx_motion.c')], check=True)
        decode = ctypes.CDLL(str(library)).t3_psx_camera_decode_compact
        decode.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                           ctypes.c_uint, ctypes.POINTER(Spline),
                           ctypes.POINTER(ctypes.c_int16)]
        decode.restype = ctypes.c_int
        streams = samples = 0
        for path in args.banks:
            data = path.read_bytes()
            _, sections = sections_of(data)
            choices = [struct.unpack_from('<hHi', data, at)
                       for at in range(sections[7], sections[8] - 7, 8)]
            offsets = sorted({script & 0xffff for _, _, script in choices if script < -1})
            for offset in offsets:
                address = sections[8] + offset
                _, stream = reference.stream_at(data, address, tab)
                encoded = data[address:address + 2 + 2 * stream.size]
                encoded_array = (ctypes.c_uint8 * len(encoded)).from_buffer_copy(encoded)
                output = (ctypes.c_int16 * 7)()
                for frame in range(stream.frames):
                    if decode(encoded_array, len(encoded), frame, ctypes.byref(spline), output) != 1:
                        raise AssertionError(f'{path}: stream {offset:#x} frame {frame}: C decode failed')
                    expected = stream.sample(frame)
                    if list(output) != expected:
                        raise AssertionError(f'{path}: stream {offset:#x} frame {frame}: '
                                             f'C={list(output)} reference={expected}')
                    samples += 1
                streams += 1
            print(f'{path}: {len(offsets)} streams verified')
        print(f'PASS: {streams} streams, {samples} frames, seven channels each')


if __name__ == '__main__':
    main()
