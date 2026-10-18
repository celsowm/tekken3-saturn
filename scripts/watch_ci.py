#!/usr/bin/env python3
"""Wait for required GitHub workflows on an exact commit using gh."""
import argparse
import json
import re
import subprocess
import time

DEFAULT_WORKFLOWS = ('Host combat regression', 'Saturn firmware and disc image')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', help='owner/repository; defaults to this checkout')
    parser.add_argument('--revision', default='HEAD')
    parser.add_argument('--workflow', action='append', help='required workflow name; repeatable')
    parser.add_argument('--timeout', type=float, default=1200, help='total seconds, including queue time')
    parser.add_argument('--interval', type=float, default=10)
    args = parser.parse_args()
    if args.timeout <= 0 or not 0 < args.interval <= 60:
        parser.error('require positive timeout and 0 < interval <= 60')
    revision = args.revision.lower() if re.fullmatch(r'[0-9a-fA-F]{40}', args.revision) else subprocess.check_output(
        ['git', 'rev-parse', '--verify', '--end-of-options', args.revision + '^{commit}'], text=True).strip()
    command = ['gh', 'run', 'list', '--commit', revision, '--limit', '100',
               '--json', 'databaseId,workflowName,status,conclusion,url,headSha']
    if args.repo:
        command += ['--repo', args.repo]
    workflows = args.workflow or DEFAULT_WORKFLOWS
    deadline = time.monotonic() + args.timeout
    previous = None
    while True:
        runs = json.loads(subprocess.check_output(command, text=True))
        # gh lists newest first: a rerun/repeated workflow must supersede old results.
        latest = {}
        for run in runs:
            if run['headSha'] == revision:
                latest.setdefault(run['workflowName'], run)
        selected = {name: latest.get(name) for name in workflows}
        status = json.dumps(selected, sort_keys=True)
        if status != previous:
            print(json.dumps(dict(commit=revision, workflows=selected), indent=2), flush=True)
            previous = status
        if all(run and run['status'] == 'completed' for run in selected.values()):
            return 0 if all(run['conclusion'] == 'success' for run in selected.values()) else 1
        if time.monotonic() >= deadline:
            parser.exit(1, 'Timed out waiting for all required workflows on this commit.\n')
        time.sleep(min(args.interval, max(0, deadline - time.monotonic())))


if __name__ == '__main__':
    raise SystemExit(main())
