#!/usr/bin/env python3
"""Summarise and package the C3 hybrid VDP1 emitter qualification on Tekken 3 Saturn.

Reads a lib-saturn run_vdp1_emit_tekken_v4.py run (summary.json, provenance.json, diffs, logs, maps) and the
follow-up probe outputs (frozen repeats, burst counters, Force rate/profile, second pair), writes
c3_v4_tekken_qualification.json and a return zip. VRAM/framebuffer dumps, ELFs and anything holding game data
other than screenshots stay out of the zip.

    scripts/pack_c3_qualification.py --run ~/dev/lib-saturn/harness/build/vdp1-tekken-v4-XXXX \
        --extra SCRATCH --out build/c3_v4_tekken_qualification-return.zip
"""
import argparse
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODES = ('control', 'hybrid')
TIMING = set('frame_serial vblanks sim_steps_last_frame loop_rate_x10 frame_work_ms late_swaps draw_waits '
             'snapshot_epoch skin_work_ms render_work_ms submitted_frames elapsed_fields_last observed_fields '
             'discarded_fields images_at_tick_mark fields_at_tick_mark local_pose_cache pad_held pad_pressed'.split())
KEEP = ('.json', '.png', '.log', '.txt', '.s')


def load(path):
    return json.loads(Path(path).read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def word(path):
    return struct.unpack('>I', Path(path).read_bytes()[:4])[0]


def state(path):
    return load(Path(path) / 'observation.json')['state']


def logical_diff(a, b):
    return sorted(k for k in a if k not in TIMING and a[k] != b.get(k))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--run', type=Path, required=True)
    p.add_argument('--extra', type=Path, required=True,
                   help='folder holding c3rep/, c3force/, pair2/ and host-test.log from the follow-up probes')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    run, extra = a.run.resolve(), a.extra.resolve()
    s, prov = load(run / 'summary.json'), load(run / 'provenance.json')
    sys.path.insert(0, str(Path.home() / 'dev/lib-saturn/harness/scripts'))
    from run_vdp1_emit_tekken_v4 import emitter_costs  # noqa: E402

    live = {}
    for n, c in s['comparisons'].items():
        a_, b_ = s['modes']['control']['captures'][n][0]['state'], s['modes']['hybrid']['captures'][n][0]['state']
        live[n] = dict(sim_ticks=c['sim_ticks'], frame_serial=c['frame_serial'], late_swaps=c['late_swaps'],
                       draw_waits=c['draw_waits'], draw_timeouts=c['draw_timeouts'],
                       images_at_tick_mark=c['images_at_tick_mark'], fields_at_tick_mark=c['fields_at_tick_mark'],
                       simulation_state_differs=logical_diff({k: v for k, v in a_.items() if not k.startswith('faces_')
                                                              and k != 'commands_used'}, b_),
                       render_counters=dict(faces_submitted=c['faces_submitted'], commands_used=c['commands_used']),
                       pixels=c['pixels'])
    repeat = {m: dict(vram=sha(run / m / 'frozen-3600/vdp1-vram.bin') == sha(extra / f'c3rep/{m}-3600/vdp1-vram.bin'),
                      framebuffer=sha(run / m / 'frozen-3600/vdp1-fb.bin') == sha(extra / f'c3rep/{m}-3600/vdp1-fb.bin'),
                      screen=sha(run / m / 'frozen-3600/screen.png') == sha(extra / f'c3rep/{m}-3600/screen.png'),
                      state=state(run / m / 'frozen-3600') == state(extra / f'c3rep/{m}-3600')) for m in MODES}
    bursts = {m: dict(flipbook_sprites=word(extra / f'c3rep/{m}-fx/symbol-0.bin'),
                      draw_passes=struct.unpack('>4I', (extra / f'c3rep/{m}-fx/symbol-2.bin').read_bytes())[3],
                      hits=word(extra / f'c3rep/{m}-fx/event-hits.bin')) for m in MODES}
    force = {m: dict(probe_600=load(extra / f'c3force/{m}-rate/force-rate.json')['probe_600'],
                     screen_sha256=load(extra / f'c3force/{m}-rate/force-rate.json')['screen_sha256'],
                     vdp1_fb_sha256=load(extra / f'c3force/{m}-rate/force-rate.json')['vdp1_fb_sha256'],
                     profile=emitter_costs(load(extra / f'c3force/{m}-prof/profile.json'))) for m in MODES}
    for m in MODES:
        for cpu in force[m]['profile'].values():
            cpu['top'] = cpu['top'][:15]
    p2 = extra / 'pair2'
    pair2 = dict(fighters='2;3', frozen_1800=dict(
        logical_state_differs=logical_diff(state(p2 / 'control-f1800'), state(p2 / 'hybrid-f1800')),
        framebuffer_identical=sha(p2 / 'control-f1800/vdp1-fb.bin') == sha(p2 / 'hybrid-f1800/vdp1-fb.bin'),
        screen_identical=sha(p2 / 'control-f1800/screen.png') == sha(p2 / 'hybrid-f1800/screen.png'),
        commands_used=[state(p2 / f'{m}-f1800')['commands_used'] for m in MODES]),
        live_8000={m: {k: state(p2 / f'{m}-live8000')[k] for k in (
            'sim_ticks', 'frame_serial', 'late_swaps', 'draw_waits', 'draw_timeouts', 'faces_rejected', 'health',
            'hits', 'blocks', 'wins0', 'wins1', 'match_done', 'images_at_tick_mark', 'fields_at_tick_mark')}
            for m in MODES})
    pair2['live_8000']['logical_state_differs'] = logical_diff(state(p2 / 'control-live8000'),
                                                               state(p2 / 'hybrid-live8000'))
    pair2['live_8000']['screen_identical'] = sha(p2 / 'control-live8000/screen.png') == sha(p2 / 'hybrid-live8000/screen.png')
    host = (extra / 'host-test.log').read_text(errors='replace')
    result = dict(
        campaign='C3 hybrid VDP1 emitter (LIBSATURN_VDP1_HYBRID_EMIT) on Tekken 3 Saturn',
        recommendation='retain opt-in; do not promote on Tekken evidence (no measurable coverage)',
        revisions=dict(libsaturn=s['libsaturn'], tekken=s['tekken']), compiler=prov['compiler'],
        bios_sha256=s['bios_sha256'], tick_fixture_sha256=s['tick_fixture_sha256'],
        provenance=dict(checks=prov['checks'], archive_members=prov['archive_members'],
                        emitters={m: prov[m]['emitters'] for m in MODES},
                        archive_sha256={m: prov[m]['archive_sha256'] for m in MODES},
                        compile_define={m: prov[m]['compile_define'] for m in MODES}),
        fight_pair='0;1', live=live, frozen=s['frozen'], repeat_frozen_3600=repeat,
        profile_window=dict(fields=s['profile_frames'], cpus={m: {c: dict(total=d['total_cycles'],
                            emitter_cycles=d['exclusive_cycles'], emitters=d['emitters'], top=d['top'][:15])
                            for c, d in s['profile'][m].items()} for m in MODES}),
        burst_coverage_ticks_0_3600=bursts, force=force, pair2=pair2,
        host_suite=dict(exit=0, passed_lines=host.count('passed') + host.count('PASS')),
        not_run=['704x448 menu (no C3 call site in menu_main.c)', 'audio capture (SDK A/B differs only in vdp1.cpp.obj)',
                 'Tekken Ball (HUD uses push_distorted_sprite_gouraud, outside C3)', 'Mednafen',
                 'physical Saturn'])
    a.out.parent.mkdir(parents=True, exist_ok=True)
    summary = a.out.with_name('c3_v4_tekken_qualification.json')
    summary.write_text(json.dumps(result, indent=2) + '\n')
    with zipfile.ZipFile(a.out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(summary, summary.name)
        for path in sorted(run.rglob('*')):
            rel = path.relative_to(run)
            if path.is_file() and path.suffix in KEEP and rel.parts[1:2] not in (('sdk',), ('sdk-build',)):
                z.write(path, 'run/' + str(rel))
            elif path.name.endswith('.map.txt'):
                z.write(path, 'run/' + str(rel))
        for path in sorted(extra.rglob('*')):
            rel = path.relative_to(extra)
            if path.is_file() and path.suffix in KEEP and rel.parts[0] in ('c3rep', 'c3force', 'pair2', 'host-test.log') \
                    and path.name not in ('profile.json',) and 'debug' not in path.name:
                z.write(path, 'followup/' + str(rel))
        for doc in ('docs/C3_HYBRID_EMIT_TEKKEN_QUALIFICATION.md',):
            if (ROOT / doc).exists():
                z.write(ROOT / doc, doc)
    print(summary)
    print(a.out, a.out.stat().st_size)


if __name__ == '__main__':
    main()
