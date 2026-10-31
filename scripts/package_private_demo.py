#!/usr/bin/env python3
"""Package a verified local Paul–Law disc for private evaluation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
README = '''TEKKEN 3 V0.1 — private evaluation build

Open music.cue in a Saturn emulator with a separately supplied compatible
Saturn BIOS. Keep music.bin beside the CUE: it contains both the data track
and the stage music. No emulator or BIOS is included.

Press START on controller 1 at the title, then select ARCADE MODE. With port 2 empty, Paul fights a
CPU-controlled Law; connect a second controller for two-player play.
Directional pad: move/step; X/Y: left/right punch; A/B: left/right kick;
L: guard; R: crouch; C: jump; START: reset the match.
At the title menu, select OPTION MODE to turn the DEBUG overlay on or off.
Press START or B to return, then select the playable mode.

Example Mednafen keyboard layout (configure it in the emulator): arrow keys
move/step, A/S punch, Z/X kick, C jump, Q guard, W crouch, and Return START.
Bindings depend on your emulator configuration.

This is an unofficial work in progress, not affiliated with Bandai Namco.
This local image contains retail-derived game assets and a boot image whose
redistribution has not been established. It is for private review, not an
approved public release. See NOTICE.md and LICENSE for code provenance.
'''


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def package(build, suite_path, boot_path, output):
    build, output = build.resolve(), output.resolve()
    if (ROOT / 'build').resolve() not in output.parents:
        raise ValueError('output must be under build/')
    if output.exists():
        raise ValueError('output already exists')
    cue, binary, elf = build / 'disc/music.cue', build / 'disc/music.bin', build / 'tekken3_saturn.elf'
    cue_text = cue.read_text()
    match = re.fullmatch(r'FILE "([^"/\\]+)" BINARY\n(?:.*\n)+', cue_text)
    if not match or match.group(1) != binary.name or 'TRACK 02 AUDIO' not in cue_text:
        raise ValueError('mixed-mode CUE must refer to the local music.bin')
    hashes = {'elf': sha(elf), 'cue': sha(cue), 'binary': sha(binary)}
    suite, boot = json.loads(suite_path.read_text()), json.loads(boot_path.read_text())
    cases = suite.get('cases', {})
    if not suite.get('passed') or len(cases) != 26 or not suite.get('native_round_replay_victory_validated') or not suite.get('original_vs_timeout_scoring_validated'):
        raise ValueError('a complete passing 26-case combat suite is required')
    for name, case in cases.items():
        provenance = case.get('provenance', {})
        if (case.get('elf_sha256') != hashes['elf'] or provenance.get('disc_sha256') != hashes['cue'] or
                provenance.get('disc_files_sha256', {}).get(binary.name) != hashes['binary']):
            raise ValueError('combat evidence does not match disc and ELF: ' + name)
    tracks = boot.get('cue_files', [])
    if (boot.get('returncode') != 0 or boot.get('tekken3_saturn.elf_sha256') != hashes['elf'] or
            boot.get('cue_sha256') != hashes['cue'] or len(tracks) != 1 or tracks[0].get('sha256') != hashes['binary']):
        raise ValueError('Mednafen boot evidence does not match disc and ELF')
    manifest = dict(scope='Private evaluation; distribution rights unresolved', hashes=hashes,
                    combat_cases=len(cases), mednafen_returncode=boot['returncode'])
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + '.tmp')
    try:
        with zipfile.ZipFile(temporary, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            archive.write(cue, cue.name)
            archive.write(binary, binary.name)
            archive.writestr('README.txt', README)
            archive.write(ROOT / 'LICENSE', 'LICENSE')
            archive.write(ROOT / 'NOTICE.md', 'NOTICE.md')
            archive.write(ROOT / 'third_party/Tekken3Remake/LICENSE.md', 'Tekken3Remake-LICENSE.md')
            archive.writestr('manifest.json', json.dumps(manifest, indent=2) + '\n')
        with zipfile.ZipFile(temporary) as archive:
            for name, expected in ((cue.name, hashes['cue']), (binary.name, hashes['binary'])):
                digest = hashlib.sha256()
                with archive.open(name) as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b''):
                        digest.update(block)
                if digest.hexdigest() != expected:
                    raise ValueError('archive checksum mismatch: ' + name)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return dict(path=str(output), bytes=output.stat().st_size, sha256=sha(output), **manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--suite', type=Path, required=True)
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.build, args.suite, args.boot, args.out), indent=2))
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
