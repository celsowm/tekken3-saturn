#!/usr/bin/env python3
"""Prepare the private Paul–Law build inputs from a user-supplied USA data track."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'
RECORDS = '10,36,56,71,72,73,74,75,79,80,81,82,279'


def prepare(source, out, music_cue=None):
    source, out = source.resolve(), out.resolve()
    private_root = (ROOT / 'assets/imported').resolve()
    if private_root not in out.parents:
        raise ValueError('output must be below private assets/imported')
    if out.exists() and any(out.iterdir()):
        raise ValueError('use an empty output directory')
    if not source.is_file():
        raise FileNotFoundError(source)
    if music_cue is not None and not music_cue.is_file():
        raise FileNotFoundError(music_cue)
    out.mkdir(parents=True, exist_ok=True)
    exe, tables = out / 'SLUS_004.02', out / 'pose-tables'
    commands = [
        ['psx_disc_import.py', '--source', str(source), '--output', str(out),
         '--extract-ids', RECORDS, '--dump-exe'],
        ['extract_psx_pose_tables.py', '--exe', str(exe), '--output', str(tables)],
        ['extract_psx_ai_tables.py', '--exe', str(exe), '--output', str(tables / 'psx_ai.bin')],
        ['extract_psx_sound_tables.py', '--exe', str(exe), '--output', str(tables / 'psx_sounds.bin')],
        ['extract_psx_effect_tables.py', '--exe', str(exe), '--out', str(tables / 'psx_effects.bin')],
        ['extract_psx_sequences.py', '--exe', str(exe), '--output', str(out / 'sequences.json')],
    ]
    for record in (74, 82, 279):
        commands.append(['divmot_to_logic.py', '--bank', str(out / 'raw' / f'{record:03}.bin'),
                         '--output', str(out / 'divmot' / f'{record:03}'),
                         '--with-motion', '--with-hitboxes', '--with-events', '--with-sounds'])
    if music_cue is not None:
        commands.append(['extract_psx_stage_music.py', '--image', str(music_cue.resolve()),
                         '--out', str(out / 'stage-music')])
    with (out / 'prepare.log').open('w') as log:
        for args in commands:
            command = [sys.executable, str(SCRIPTS / args[0]), *args[1:]]
            log.write(json.dumps(command) + '\n')
            log.flush()
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    required = [exe, out / 'raw/079.bin', out / 'raw/081.bin', out / 'raw/082.bin',
                out / 'divmot/074/bank_0.t3logic', out / 'divmot/082/bank_1.t3logic',
                out / 'divmot/279/bank_99.t3logic', tables / 'psx_sounds.bin',
                tables / 'psx_effects.bin', tables / 'psx_ai.bin', out / 'sequences.json']
    if any(not path.is_file() for path in required):
        raise ValueError('import completed without all expected files')
    report = dict(output=str(out), records=RECORDS, music=music_cue is not None,
                  scope='Private retail-derived input; do not distribute the generated files.')
    (out / 'prepare.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='USA disc data-track BIN or ISO')
    parser.add_argument('--out', type=Path, required=True, help='new directory under assets/imported')
    parser.add_argument('--music-cue', type=Path, help='USA CUE for optional Paul stage music')
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.source, args.out, args.music_cue), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}; inspect prepare.log if present\n')


if __name__ == '__main__':
    main()
