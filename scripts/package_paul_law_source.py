#!/usr/bin/env python3
"""Create a source-only local build kit, excluding ignored and retail data.

This is a build kit, not a ready-to-play disc or a permission to publish it.
It includes the tracked files from this worktree and the pinned research
submodule, plus the explicitly listed new files for the current demo work.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SUBMODULE = Path('third_party/Tekken3Remake')
EXTRA = (
    'scripts/build_paul_law_demo.py', 'scripts/build_stage_cdda_disc.py',
    'scripts/build_cd_audio.py', 'scripts/convert_cd_audio.py',
    'scripts/check_demo_sdk.py',
    'scripts/analyze_loading_capture.py',
    'scripts/analyze_fight_cadence.py',
    'scripts/extract_title_art.py',
    'scripts/mednafen_input_script.swift', 'scripts/package_itch_demo.py',
    'scripts/mednafen_press_start.swift',
    'scripts/configure_mednafen_controls.py',
    'scripts/package_private_demo.py',
    'scripts/check_cdda_capture.py', 'scripts/extract_psx_stage_music.py',
    'scripts/inspect_psx_sound_ids.py', 'scripts/prepare_paul_law_assets.py',
    'scripts/package_paul_law_source.py', 'src/saturn/stage_cdda.c', 'src/saturn/stage_cdda.h',
    'tests/test_build_stage_cdda_disc.py', 'tests/test_check_cdda_capture.py',
    'tests/test_configure_mednafen_controls.py',
    'tests/test_package_private_demo.py',
    'tests/test_extract_psx_stage_music.py', 'tests/test_prepare_paul_law_assets.py',
    'tests/test_print_release_configure.py', 'tests/test_saturn_regression_disc.py',
)
FORBIDDEN = ('.git/', 'build/', 'roms/', 'assets/imported/',
             'third_party/Tekken3Remake/work/', 'third_party/Tekken3Remake/remake/imported/')


def tracked(root):
    raw = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z'])
    return [Path(name.decode('utf-8')) for name in raw.split(b'\0') if name]


def package(out):
    out = out.resolve()
    if (ROOT / 'build').resolve() not in out.parents:
        raise ValueError('source kit output must be under build/')
    if out.exists():
        raise ValueError('output exists; choose a new path')
    subroot = ROOT / SUBMODULE
    if subprocess.check_output(['git', '-C', str(subroot), 'status', '--porcelain', '--untracked-files=no']):
        raise ValueError('research submodule has tracked changes; review them first')
    paths = {p for p in tracked(ROOT) if p != SUBMODULE}
    paths.update(SUBMODULE / p for p in tracked(subroot))
    paths.update(Path(p) for p in EXTRA)
    entries = []
    for rel in sorted(paths):
        name = rel.as_posix()
        path = ROOT / rel
        if rel.is_absolute() or '..' in rel.parts or any(name.startswith(p) for p in FORBIDDEN):
            raise ValueError('refusing excluded or unsafe path: ' + name)
        if path.is_symlink() or not path.is_file() or ROOT not in path.resolve().parents:
            raise ValueError('missing or unsafe source file: ' + name)
        entries.append((name, path))
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    subrevision = subprocess.check_output(['git', '-C', str(subroot), 'rev-parse', 'HEAD'], text=True).strip()
    out.parent.mkdir(parents=True, exist_ok=True)
    temporary = out.with_suffix(out.suffix + '.tmp')
    manifest = dict(project_revision=revision, research_revision=subrevision,
                    scope='Source build kit; user-supplied game data and separate SDK/toolchain required.', files={})
    try:
        with zipfile.ZipFile(temporary, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, path in entries:
                data = path.read_bytes()
                archive.writestr('tekken3-saturn/' + name, data)
                manifest['files'][name] = hashlib.sha256(data).hexdigest()
            archive.writestr('tekken3-saturn/SOURCE_KIT_MANIFEST.json', json.dumps(manifest, indent=2) + '\n')
        temporary.replace(out)
    finally:
        temporary.unlink(missing_ok=True)
    return dict(path=str(out), files=len(entries), project_revision=revision,
                research_revision=subrevision, bytes=out.stat().st_size,
                sha256=hashlib.sha256(out.read_bytes()).hexdigest(), scope=manifest['scope'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True, help='new ZIP path under build/')
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.out), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
