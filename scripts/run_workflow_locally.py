#!/usr/bin/env python3
"""Run a GitHub workflow's `run:` steps here, in a throwaway worktree of HEAD.

Finds every failing step in one go instead of one per push. The worktree gets
links to the pinned submodule and to the local SDK as .external/libsaturn (no
SDK copy, see AGENTS.md), and `gcc` means --gcc, because macOS's gcc is clang.
It is not GitHub: checkouts, actions and apt steps are skipped, so a pass here
does not replace the real run. The SDK checkout must be the pinned commit.

    scripts/run_workflow_locally.py .github/workflows/host.yml --work /tmp/ci
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('workflow', type=Path)
    p.add_argument('--work', type=Path, required=True, help='new directory for the worktree')
    p.add_argument('--libsaturn', type=Path, default=ROOT.parent / 'lib-saturn')
    p.add_argument('--gcc', default='gcc-16')
    p.add_argument('--skip', action='append', default=[], help='skip steps whose name contains this text')
    args = p.parse_args()
    work = args.work.resolve()
    subprocess.run(['git', 'worktree', 'add', '-q', '--detach', str(work), 'HEAD'], cwd=ROOT, check=True)
    failed = []
    try:
        submodule = work / 'third_party/Tekken3Remake'
        shutil.rmtree(submodule, ignore_errors=True)
        submodule.symlink_to(ROOT / 'third_party/Tekken3Remake')
        (work / '.external').mkdir()
        (work / '.external/libsaturn').symlink_to(args.libsaturn.resolve())
        shims = work / '.external/bin'
        shims.mkdir()
        (shims / 'gcc').symlink_to(shutil.which(args.gcc))
        for name in ('python', 'python3'):  # a script, because a link to a venv's python would leave the venv
            (shims / name).write_text(f'#!/bin/sh\nexec "{sys.executable}" "$@"\n')
            (shims / name).chmod(0o755)
        env = dict(os.environ, PATH=f'{shims}{os.pathsep}{os.environ["PATH"]}')
        for job_name, job in yaml.safe_load(args.workflow.read_text())['jobs'].items():
            for step in job['steps']:
                name = step.get('name', '')
                if 'run' not in step or any(text in name for text in args.skip):
                    continue
                result = subprocess.run(['bash', '-e', '-c', step['run']], cwd=work, capture_output=True, text=True,
                                        env={**env, **{k: str(v) for k, v in step.get('env', {}).items()}})
                print(f"{'ok  ' if result.returncode == 0 else 'FAIL'} {job_name}: {name}", flush=True)
                if result.returncode:
                    failed.append(name)
                    print((result.stdout + result.stderr)[-1500:], flush=True)
    finally:
        subprocess.run(['git', 'worktree', 'remove', '--force', str(work)], cwd=ROOT, check=True)
    print(f'{len(failed)} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
