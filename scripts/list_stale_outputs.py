#!/usr/bin/env python3
"""List experiment outputs under build/ and assets/imported/ that nothing names.

An entry is stale when no tracked file in this repository names it, directly or
through a wildcard such as `recovery-*`, and AGENTS.md does not say to keep it.
This only prints paths, one per line, with their total on stderr; it deletes
nothing, so read the list before removing anything (see AGENTS.md).

    scripts/list_stale_outputs.py > stale.txt
"""
import fnmatch
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOKEN = re.compile(r'[A-Za-z0-9._*-]+')
BRACES = re.compile(r'([A-Za-z0-9._*-]*)\{([A-Za-z0-9._*,-]+)\}([A-Za-z0-9._*-]*)')
ALWAYS_KEEP = {
    'assets/imported': ['tekken3-usa', 'ymir', 'saturn-bios', 'paul-reference-*', 'frozen*'],
    'build': ['asset-*', 'paul-blend*', 'saturn', 'host*', 'roster', 'tekken3-*', 'demo-source-kit.zip', 'test-venv'],
}


def named_in_tracked_files():
    names = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, check=True).stdout.split(b'\0')
    tokens = set()
    for name in names:
        path = ROOT / name.decode()
        if name and path.is_file():
            data = path.read_bytes()
            if b'\0' not in data:
                text = data.decode(errors='replace')
                tokens.update(TOKEN.findall(text))
                # `name-{control,pipeline}-cd-*` names each alternative
                for head, options, tail in BRACES.findall(text):
                    tokens.update(f'{head}{option}{tail}' for option in options.split(','))
    return tokens


def size(path):
    if path.is_symlink() or path.is_file():
        return path.lstat().st_size
    return sum(p.lstat().st_size for p in path.rglob('*'))


def main():
    tokens = named_in_tracked_files()
    # a wildcard counts only with a real name before it: `recovery-*`, not `*.log`
    wildcards = [t for t in tokens if t.find('*') >= 8]
    total = 0
    for folder, keep in ALWAYS_KEEP.items():
        for entry in sorted((ROOT / folder).iterdir()):
            name = entry.name
            if name in tokens or any(fnmatch.fnmatch(name, p) for p in keep + wildcards):
                continue
            total += size(entry)
            print(entry.relative_to(ROOT))
    print(f'{total / 1048576:.0f} MB', file=sys.stderr)


if __name__ == '__main__':
    main()
