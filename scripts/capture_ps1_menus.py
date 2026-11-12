#!/usr/bin/env python3
"""Save the original PS1 title, main menu and character select as PNG.

Starts PCSX-Redux on the USA disc with pcsx_redux_menu_capture.lua, which pauses
at fixed points; each pause is saved from /api/v1/screen/still and resumed.
Read-only: no guest writes. Output goes under assets/imported or build.
"""
import argparse
import os
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['title-fade', 'title', 'menu', 'select-a', 'select-b', 'select-c']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', type=Path, default=Path.home() / 'dev/pcsx-redux/pcsx-redux')
    p.add_argument('--out', type=Path, default=ROOT / 'build/ps1-menus')
    p.add_argument('--port', type=int, default=8083)
    p.add_argument('--lua', type=Path, default=ROOT / 'scripts/pcsx_redux_menu_capture.lua', help='the capture plan')
    p.add_argument('--names', nargs='+', default=NAMES, help='its pause names, in order')
    p.add_argument('--wait', type=float, default=300, help='seconds to wait for each pause')
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    url = f'http://127.0.0.1:{args.port}/api/v1/'
    log = (out / 'emulator.log').open('w')
    proc = subprocess.Popen(
        [str(args.emulator), '-no-ui', '-interpreter', '-softgpu',
         '-bios', str(Path.home() / 'dev/pcsx-redux/src/mips/openbios/openbios.bin'),
         '-iso', str(ROOT / 'roms/Tekken 3 USA/Tekken 3 (USA).cue'),
         '-run', '-webserver', '-webserver-port', str(args.port),
         '-dofile', str(args.lua.resolve())],
        env={**os.environ, 'SDL_AUDIODRIVER': 'dummy'}, stdout=log, stderr=log, cwd=ROOT)
    try:
        for name in args.names:
            deadline = time.time() + args.wait
            while True:
                try:
                    flow = urllib.request.urlopen(url + 'execution-flow', timeout=5).read()
                    if b'"running":false' in flow.replace(b' ', b''):
                        break
                except OSError:
                    pass
                if time.time() > deadline:
                    raise SystemExit(f'timed out waiting for {name}')
                time.sleep(0.5)
            (out / f'{name}.png').write_bytes(urllib.request.urlopen(url + 'screen/still', timeout=10).read())
            print('saved', name, flush=True)
            urllib.request.urlopen(urllib.request.Request(
                url + 'execution-flow?function=resume', method='POST'), timeout=5).read()
    finally:
        proc.terminate()


if __name__ == '__main__':
    main()
