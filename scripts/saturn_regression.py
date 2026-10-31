#!/usr/bin/env python3
"""Run the release gates for one Saturn build and write a single JSON verdict.

The gates are the commands in docs/RELEASE.md plus the two optional
comparisons that need their own fixtures: the 3,000-tick original reference
replay and a frozen-pixel presentation pair. Every gate runs as a subprocess
in its own log, so one failed gate cannot be mistaken for another, and the
summary is written after each stage so an interrupted run still says what it
had proved.

A pass here certifies host behavior, live Ymir gameplay and (when selected)
the declared fixtures. It does not certify CD boot, display cadence or a
visual match outside the compared tick.

All outputs belong under an ignored path; the disc images and captures
contain retail-derived data.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'
DEFAULT_BIOS = Path(os.environ['LIBSATURN_BIOS']) if os.environ.get('LIBSATURN_BIOS') else None


def digest(path):
    sha = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            sha.update(block)
    return sha.hexdigest()


def read_json(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return {}


def slug(name):
    return re.sub(r'[^A-Za-z0-9_.-]+', '-', name).strip('-')


def run_stage(name, command, out, detail=None):
    """Execute one gate, keep its log and return its summary record."""
    log = out / (slug(name) + '.log')
    started = time.monotonic()
    error = None
    code = None
    with log.open('w') as stream:
        stream.write(json.dumps(command) + '\n\n')
        stream.flush()
        try:
            code = subprocess.run(command, cwd=ROOT, stdout=stream,
                                  stderr=subprocess.STDOUT).returncode
        except OSError as failure:
            error = str(failure)
    if error is not None:
        status = 'failed'
    else:
        status = 'passed' if code == 0 else 'failed'
    record = dict(name=name, status=status, seconds=round(time.monotonic() - started, 2),
                  command=command, log=str(log), exit_code=code, detail=detail or {})
    if error is not None:
        record['spawn_error'] = error
    return record


def host_stage(build, out):
    ctest = shutil.which('ctest')
    name = 'host:' + str(build)
    if ctest is None:
        return dict(name=name, status='failed', seconds=0.0, command=[],
                    log='', exit_code=None, detail={'error': 'ctest not on PATH'})
    if not (build / 'CTestTestfile.cmake').is_file():
        return dict(name=name, status='failed', seconds=0.0, command=[],
                    log='', exit_code=None,
                    detail={'error': 'no configured host build at ' + str(build)})
    record = run_stage(name, [ctest, '--test-dir', str(build), '--output-on-failure'], out)
    text = Path(record['log']).read_text(errors='replace')
    match = re.search(r'(\d+)% tests passed, (\d+) tests failed out of (\d+)', text)
    if match:
        record['detail'].update(percent=int(match.group(1)), failed=int(match.group(2)),
                                tests=int(match.group(3)))
    elif (match := re.search(r'(\d+)% tests passed out of (\d+)', text)):
        record['detail'].update(percent=int(match.group(1)), failed=0, tests=int(match.group(2)))
    elif record['status'] == 'passed':
        record['status'] = 'failed'
        record['detail']['error'] = 'ctest printed no test census'
    return record


def combat_stage(args, out):
    root = out / 'combat'
    command = [sys.executable, str(SCRIPTS / 'saturn_combat_suite.py'),
               '--build', str(args.build), '--workload', args.workload,
               '--bios', str(args.bios), '--out', str(root),
               '--with-ko', '--with-timeout']
    if args.sh2_cache:
        command += ['--sh2-cache', args.sh2_cache]
    if args.disc:
        command += ['--disc', str(args.disc)]
    if getattr(args, 'title_start_field', None) is not None:
        command += ['--title-start-field', str(args.title_start_field)]
    record = run_stage('combat', command, out)
    suite = read_json(root / 'suite.json')
    cases = suite.get('cases', {})
    record['detail'].update(cases=len(cases), case_names=sorted(cases),
                            input_offset_fields=suite.get('input_offset_fields'),
                            input_offset_measured=suite.get('input_offset_measured'),
                            title_start_field=suite.get('title_start_field'),
                            native_round_replay_victory_validated=suite.get('native_round_replay_victory_validated'),
                            original_vs_timeout_scoring_validated=suite.get('original_vs_timeout_scoring_validated'))
    if record['exit_code'] not in (None, 0) and not cases:
        failed = read_json(root / 'failed.json')
        record['detail']['failed_cases'] = sorted(failed)
    return record


def reference_stage(args, out):
    root = out / 'reference'
    command = [sys.executable, str(SCRIPTS / 'saturn_reference_suite.py'),
               '--build', str(args.reference_build), '--workload', args.workload,
               '--bios', str(args.bios), '--out', str(root),
               '--export', str(args.reference_export)]
    if args.sh2_cache:
        command += ['--sh2-cache', args.sh2_cache]
    record = run_stage('reference', command, out)
    result = read_json(root / 'reference-comparison.json')
    if result:
        record['detail'].update(
            ticks=result.get('ticks'), expected_ticks=result.get('expected_ticks'),
            matching_prefix=result.get('matching_prefix'),
            first_difference=result.get('first_difference'),
            elf_sha256=result.get('elf_sha256'),
            trace_sha256=result.get('trace_sha256'),
            guest_trace_sha256=result.get('guest_trace_sha256'),
            fixture_manifest_sha256=result.get('fixture_manifest_sha256'))
    return record


def frozen_stage(args, out):
    command = [sys.executable, str(SCRIPTS / 'compare_saturn_presentation.py'),
               str(args.frozen_baseline), str(args.frozen_candidate)]
    if args.include_cached_stage_faces:
        command.append('--include-cached-stage-faces')
    record = run_stage('frozen', command, out)
    text = Path(record['log']).read_text(errors='replace') if record['log'] else ''
    # The comparator prints its report only on success; keep the first JSON
    # value so a warning after it cannot invalidate the verdict.
    start = text.find('{')
    if record['status'] == 'passed' and start >= 0:
        try:
            report, _ = json.JSONDecoder().raw_decode(text[start:])
            record['detail'].update(report)
        except ValueError:
            record['status'] = 'failed'
            record['detail']['error'] = 'comparator printed an unreadable report'
    return record


def particle_stage(args, out):
    root = out / 'particle'
    command = [sys.executable, str(SCRIPTS / 'saturn_particle_suite.py'),
               '--build', str(args.particle_build), '--bios', str(args.bios), '--out', str(root)]
    if args.particle_real_build:
        command += ['--real-build', str(args.particle_real_build)]
    record = run_stage('particle', command, out)
    result = read_json(root / 'particle-suite.json')
    if result:
        record['detail'].update(checks=result.get('checks'), values=result.get('values'))
    return record


def preflight(args, parser):
    problems = []
    elf = args.build / 'tekken3_saturn.elf'
    if not elf.is_file():
        problems.append('no built ELF at ' + str(elf))
    if args.disc and not args.disc.is_file():
        problems.append('no selected disc at ' + str(args.disc))
    if not args.skip_combat and not args.bios:
        problems.append('--bios (or LIBSATURN_BIOS) is required by the combat gate')
    if not args.skip_reference:
        if not args.reference_build:
            problems.append('--reference-build is required unless --skip-reference')
        if not args.reference_export:
            problems.append('--reference-export is required unless --skip-reference')
        if args.bios is None:
            problems.append('--bios (or LIBSATURN_BIOS) is required by the reference gate')
        elif args.reference_build and not (args.reference_build / 'tekken3_saturn.elf').is_file():
            problems.append('no reference ELF at ' + str(args.reference_build / 'tekken3_saturn.elf'))
        if args.reference_export and not (args.reference_export / 'manifest.json').is_file():
            problems.append('no reference export manifest at ' + str(args.reference_export))
    if args.frozen_baseline or args.frozen_candidate:
        if not (args.frozen_baseline and args.frozen_candidate):
            problems.append('--frozen-baseline and --frozen-candidate go together')
        else:
            for label, capture in (('baseline', args.frozen_baseline), ('candidate', args.frozen_candidate)):
                for filename in ('observation.json', 'screen.png', 'ymir.json'):
                    if not (capture / filename).is_file():
                        problems.append('frozen ' + label + ' lacks ' + str(capture / filename))
        if importlib.util.find_spec('PIL') is None:
            problems.append('the frozen comparison needs Pillow; run with build/asset-venv/bin/python')
    if args.reference_build and args.reference_build.resolve() == args.build.resolve():
        problems.append('the reference oracle and the release build must be different directories')
    if problems:
        parser.error('\n  '.join(['invalid gate selection:'] + problems))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/saturn',
                        help='release build directory containing tekken3_saturn.elf')
    parser.add_argument('--disc', type=Path, help='alternate ISO or mixed-mode CUE for the combat gate')
    parser.add_argument('--workload', choices=('paul-law-release', 'two-paul-release', 'diagnostic'),
                        default='paul-law-release')
    parser.add_argument('--bios', type=Path, default=DEFAULT_BIOS,
                        help='Saturn BIOS image; defaults to $LIBSATURN_BIOS')
    parser.add_argument('--libsaturn', type=Path, default=ROOT.parent / 'lib-saturn',
                        help='LibSaturn tree holding harness/build-macos/probe')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/imported/saturn-regression')
    parser.add_argument('--force', action='store_true', help='replace a non-empty --out directory')
    parser.add_argument('--label', default='', help='free-form name recorded in the summary')
    parser.add_argument('--sh2-cache', choices=('on', 'off'),
                        help='pass an explicit Ymir cache setting to the live gates')
    parser.add_argument('--title-start-field', type=int,
                        help='send START at this field in each title-screen combat run')
    parser.add_argument('--host-build', type=Path, action='append', default=[],
                        help='host CTest build to gate; repeatable')
    parser.add_argument('--skip-host', action='store_true')
    parser.add_argument('--skip-combat', action='store_true')
    parser.add_argument('--skip-reference', action='store_true')
    parser.add_argument('--fail-fast', action='store_true',
                        help='stop after the first failed gate instead of collecting every failure')
    parser.add_argument('--reference-build', type=Path,
                        help='oracle build that carries the reference trace')
    parser.add_argument('--reference-export', type=Path,
                        help='original-code export the trace is compared against')
    parser.add_argument('--particle-build', type=Path,
                        help='spray-fixture build (T3_PARTICLES plus T3_PARTICLE_SPRAY_SELFTEST, '
                             'T3_PARTICLE_CELL_CHECK, T3_PARTICLE_DROP_SELFTEST) for the particle case')
    parser.add_argument('--particle-real-build', type=Path,
                        help='T3_PARTICLES build without fixtures; the particle stage also lets the Law AI spawn a real spray')
    parser.add_argument('--frozen-baseline', type=Path, help='frozen capture used as the pixel baseline')
    parser.add_argument('--frozen-candidate', type=Path, help='frozen capture from the gated build')
    parser.add_argument('--include-cached-stage-faces', action='store_true',
                        help='reconcile published stage replay counts in the frozen comparison')
    args = parser.parse_args(argv)

    args.build = args.build.resolve()
    args.disc = args.disc.resolve() if args.disc else None
    args.out = args.out.resolve()
    if args.bios:
        args.bios = args.bios.resolve()
    args.reference_build = args.reference_build.resolve() if args.reference_build else None
    args.reference_export = args.reference_export.resolve() if args.reference_export else None
    args.particle_build = args.particle_build.resolve() if args.particle_build else None
    args.particle_real_build = args.particle_real_build.resolve() if args.particle_real_build else None
    args.frozen_baseline = args.frozen_baseline.resolve() if args.frozen_baseline else None
    args.frozen_candidate = args.frozen_candidate.resolve() if args.frozen_candidate else None
    preflight(args, parser)

    if not args.skip_host and not args.host_build:
        args.host_build = [ROOT / 'build/host']
        second = ROOT / 'build/host-law'
        if (second / 'CTestTestfile.cmake').is_file():
            # The second character bank is private to this machine; gate it when
            # configured so a Law-side host regression cannot slip through.
            args.host_build.append(second)
    else:
        args.host_build = [path.resolve() for path in args.host_build]

    if args.out.exists():
        if any(args.out.iterdir()):
            if not args.force:
                parser.error(str(args.out) + ' is not empty; choose a fresh --out or pass --force')
            shutil.rmtree(args.out)
    args.out.mkdir(parents=True, exist_ok=True)

    started = time.time()
    summary = dict(schema=1, label=args.label, workload=args.workload,
                   started_at=datetime.fromtimestamp(started, timezone.utc)
                                .isoformat(timespec='seconds'),
                   out=str(args.out), python=sys.executable, complete=False,
                   build=dict(path=str(args.build),
                              elf_sha256=digest(args.build / 'tekken3_saturn.elf')),
                   stages=[], passed=False)
    if args.bios:
        summary['bios'] = dict(path=str(args.bios), sha256=digest(args.bios))
    if args.disc:
        from saturn_probe import disc_provenance
        summary['disc'] = dict(path=str(args.disc), **disc_provenance(args.disc))

    def publish():
        summary['duration_seconds'] = round(time.time() - started, 2)
        failed = [stage['name'] for stage in summary['stages'] if stage['status'] == 'failed']
        summary['failed_stages'] = failed
        summary['passed'] = not failed and summary['complete']
        (args.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')

    def record(stage):
        summary['stages'].append(stage)
        publish()
        print('%-28s %-7s %8.2fs' % (stage['name'], stage['status'], stage.get('seconds', 0.0)),
              flush=True)
        return stage['status'] == 'passed'

    stages = []
    if not args.skip_host:
        stages += [(lambda build=build: host_stage(build, args.out)) for build in args.host_build]
    if not args.skip_combat:
        stages.append(lambda: combat_stage(args, args.out))
    if not args.skip_reference:
        stages.append(lambda: reference_stage(args, args.out))
    if args.particle_build:
        stages.append(lambda: particle_stage(args, args.out))
    if args.frozen_baseline:
        stages.append(lambda: frozen_stage(args, args.out))

    ok = True
    for stage in stages:
        ok = record(stage()) and ok
        if not ok and args.fail_fast:
            break
    summary['complete'] = len(summary['stages']) == len(stages)
    publish()

    print(json.dumps(dict(passed=summary['passed'], out=str(args.out / 'summary.json'),
                          stages={stage['name']: stage['status'] for stage in summary['stages']}),
                     indent=2))
    return 0 if ok and summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
