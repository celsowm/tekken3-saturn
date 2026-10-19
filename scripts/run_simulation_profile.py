#!/usr/bin/env python3
"""Capture a simulation profile, retrying only torn guest publications."""
import copy,json
from pathlib import Path
import saturn_probe
from analyze_simulation_profile import analyze,IncompleteProfileError

def run(args):
    root=args.out.resolve()
    if root.exists() and any(root.iterdir()):raise FileExistsError('use a fresh output directory')
    root.mkdir(parents=True,exist_ok=True)
    for extra in range(3):
        attempt=copy.copy(args);attempt.out=root/f'capture-{extra}';attempt.frames=args.frames+extra
        attempt.dump_symbol=[*getattr(args,'dump_symbol',[]),'g_t3_sim_profile']
        try:
            observation=saturn_probe.run(attempt)
            report=analyze(attempt.out)
        except (IncompleteProfileError,saturn_probe.IncompleteSnapshotError):
            if extra==2:raise
            continue
        report.update(requested_fields=args.frames,actual_fields=attempt.frames,
                      publication_retries=extra,elf_sha256=observation['elf_sha256'])
        (root/'profile.json').write_text(json.dumps(report,indent=2)+'\n')
        print(f"Validated {report['aggregate_ticks']} timed ticks; {root/'profile.json'}")
        return report

if __name__=='__main__':
    parser=saturn_probe.parser();parser.set_defaults(frames=1200,timeout=300)
    run(parser.parse_args())
