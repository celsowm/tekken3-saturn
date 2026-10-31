#!/usr/bin/env python3
"""Set a comfortable Saturn keyboard layout in a local Mednafen config."""
import argparse
from datetime import datetime
from pathlib import Path
import shutil
import tempfile

# Mednafen's SDL2 scancodes, independent of the host keyboard's key codes.
KEYS = {'up': 82, 'down': 81, 'left': 80, 'right': 79, # Arrow keys
        'x': 4, 'y': 22, 'z': 7,                     # A S D
        'a': 29, 'b': 27, 'c': 6,                    # Z X C
        'ls': 20, 'rs': 26, 'start': 40}             # Q W Return


def configure(path, cpu=True):
    path = path.expanduser().resolve()
    text = path.read_text()
    values = {f'ss.input.port1.gamepad.{button}': f'keyboard 0x0 {code}'
              for button, code in KEYS.items()}
    if cpu:
        values['ss.input.port2'] = 'none'
    counts = dict.fromkeys(values, 0)
    lines = []
    for line in text.splitlines(keepends=True):
        key = line.split(None, 1)[0] if line.strip() else ''
        if key in values:
            counts[key] += 1
            ending = '\r\n' if line.endswith('\r\n') else '\n' if line.endswith('\n') else ''
            line = f'{key} {values[key]}{ending}'
        lines.append(line)
    invalid = [key for key, count in counts.items() if count != 1]
    if invalid:
        raise ValueError('expected exactly one setting for: ' + ', '.join(invalid))
    updated = ''.join(lines)
    if updated == text:
        return None
    backup = path.with_name(path.name + '.before-tekken3-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    if backup.exists():
        raise FileExistsError(backup)
    shutil.copy2(path, backup)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile('w', dir=path.parent, delete=False) as file:
            temporary = Path(file.name)
            file.write(updated)
        shutil.copymode(path, temporary)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=Path('~/.mednafen/mednafen.cfg'))
    parser.add_argument('--keep-port2', action='store_true', help='do not change the second Saturn port')
    args = parser.parse_args()
    try:
        backup = configure(args.config, cpu=not args.keep_port2)
        print(f'Updated {args.config.expanduser()}; backup: {backup}' if backup else 'Controls already configured.')
        print('Arrow keys move/step; A/S/D and Z/X/C are Saturn face buttons; Q/W shoulders; Return start/reset.')
        if not args.keep_port2:
            print('Saturn port 2 is disconnected so Law is CPU-controlled.')
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
