#!/usr/bin/env python3
"""Apply exact text replacements to several files, all or nothing.

Each old text must occur exactly once in its file (after the earlier edits
to that file). Nothing is written unless every edit applies.

Spec format (one or more blocks; paths are relative to the current
directory):

    @@@ src/game/psx_camera.c
    <<<
    old text
    ===
    new text
    >>>

A line holding only `<<<`, `===` or `>>>` is a marker, so those lines
cannot appear in the texts themselves. An empty new text deletes the old one.

    scripts/edit_once.py SPEC [--dry-run]
    scripts/edit_once.py - < SPEC
"""
import argparse
import sys


def parse(text):
    edits, path, mode, old, new = [], None, None, [], []
    for number, line in enumerate(text.splitlines(), 1):
        if mode is None and line.startswith("@@@ "):
            path = line[4:].strip()
        elif line == "<<<" and mode is None:
            if path is None:
                raise SystemExit(f"spec line {number}: edit before any @@@ path")
            mode, old, new = "old", [], []
        elif line == "===" and mode == "old":
            mode = "new"
        elif line == ">>>" and mode == "new":
            edits.append((path, "\n".join(old), "\n".join(new), number))
            mode = None
        elif mode == "old":
            old.append(line)
        elif mode == "new":
            new.append(line)
        elif line.strip():
            raise SystemExit(f"spec line {number}: text outside an edit block")
    if mode is not None:
        raise SystemExit("spec ends inside an edit block")
    return edits


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("spec", help="spec file, or - for stdin")
    parser.add_argument("--dry-run", action="store_true", help="check only")
    args = parser.parse_args()
    spec = sys.stdin.read() if args.spec == "-" else open(args.spec).read()
    files, failed = {}, 0
    for path, old, new, number in parse(spec):
        if path not in files:
            files[path] = open(path).read()
        count = files[path].count(old) if old else 0
        if count != 1:
            first = old.splitlines()[0] if old else ""
            print(f"{path}: edit ending at spec line {number} matches {count} times: {first!r}")
            failed += 1
            continue
        files[path] = files[path].replace(old, new)
    if failed:
        print(f"{failed} edits failed; no file written")
        return 1
    if not args.dry_run:
        for path, text in files.items():
            with open(path, "w") as out:
                out.write(text)
    print(f"{'checked' if args.dry_run else 'wrote'} {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
