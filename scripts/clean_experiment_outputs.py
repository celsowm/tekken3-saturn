#!/usr/bin/env python3
"""Remove explicitly named ignored experiment directories within disk-budget roots.

Dry run by default. Paths must be direct children of build/ or
assets/imported/. Mandatory baseline, SDK and private-import data are refused.
"""
import argparse
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARENTS = (ROOT / 'build', ROOT / 'assets/imported')


def protected(parent, name):
    if parent == PARENTS[0]:
        return name.startswith(('asset-', 'host')) or name in ('paul-blend', 'saturn')
    return (name in ('tekken3-usa', 'ymir', 'paul-reference-sanitized-export',
                    'presentation-baseline-frozen') or
            name.startswith('paul-reference-') and name.endswith('-export') or
            'frozen-pixel-baseline' in name)


def validate(raw):
    path = raw if raw.is_absolute() else ROOT / raw
    # Refuse symlinks at the target or parent. Resolve only after checking the
    # lexical parent so a relative path cannot escape through "..".
    parent = path.parent
    if parent not in PARENTS or parent.is_symlink() or path.is_symlink():
        raise ValueError(f'expected a direct, nonsymlink child of build/ or assets/imported/: {raw}')
    if protected(parent, path.name):
        raise ValueError(f'mandatory data is protected: {raw}')
    if not path.is_dir():
        raise ValueError(f'not an existing experiment directory: {raw}')
    ignored = subprocess.run(['git', 'check-ignore', '-q', '--', str(path)],
                             cwd=ROOT, check=False)
    if ignored.returncode != 0:
        raise ValueError(f'not a git-ignored experiment directory: {raw}')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', type=Path, nargs='+', help='explicit experiment directories')
    parser.add_argument('--delete', action='store_true', help='actually remove the listed directories')
    args = parser.parse_args()
    try:
        paths = [validate(raw) for raw in args.paths]
    except ValueError as error:
        parser.error(str(error))
    if len(set(paths)) != len(paths):
        parser.error('duplicate directory')
    for path in paths:
        print(('deleting ' if args.delete else 'would delete ') + str(path))
    if args.delete:
        for path in paths:
            shutil.rmtree(path)


if __name__ == '__main__':
    main()
