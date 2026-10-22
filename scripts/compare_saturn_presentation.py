#!/usr/bin/env python3
"""Compare frozen presentation captures at the same committed simulation tick."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops

# The HUD's serial and timing change as renderer throughput changes. Everything
# else, including all PS1 pose/combat fields, must agree at the selected tick.
VARIABLE_FIELDS = frozenset((
    'frame_serial', 'vblanks', 'sim_steps_last_frame', 'loop_rate_x10',
    'frame_work_ms', 'render_work_ms', 'skin_work_ms', 'submitted_frames',
    'snapshot_epoch', 'late_swaps', 'draw_waits', 'commands_used',
    'elapsed_fields_last', 'observed_fields', 'discarded_fields', 'scheduled_ticks',
    'local_pose_cache'))
MODEL_CROP = (0, 61, 320, 196)
BOOT_FIELDS = ('sh2_cache_emulation', 'nominal_refresh_hz',
               'sh2_clock_factor_num', 'sh2_clock_factor_den')


def compare(baseline, candidate, include_cached_stage_faces=False):
    observations = [json.loads((p/'observation.json').read_text())
                    for p in (baseline, candidate)]
    states = [o['state'] for o in observations]
    resident_bytes=[s.get('model_texture_vram_bytes') for s in states]
    for i,o in enumerate(observations):
        shared=o.get('optimizations',{}).get('shared_actor_resources',0)
        if shared not in (0,1):raise ValueError('invalid shared actor resource count')
        if shared:
            workload=o.get('workload',{})
            if workload.get('configuration',{}).get('T3_SHARE_ACTOR_RESOURCES') not in ('ON','1','TRUE') or \
                    workload.get('original_textures') is not True or \
                    not isinstance(resident_bytes[i],int) or resident_bytes[i]<=0:
                raise ValueError('shared textures lack original-asset execution evidence')
            # Compare the same two logical actors while retaining the actual
            # resident-byte reduction in the report. Never ignore this field.
            states[i]={**states[i],'model_texture_vram_bytes':resident_bytes[i]*2}
    cached_faces=[0,0]
    if include_cached_stage_faces:
        # Native stage replay is accounted separately by the scene facade.
        # Reconcile the exact published count, never ignore geometry totals.
        for i,observation in enumerate(observations):
            counters=observation.get('optimizations',{})
            count=counters.get('cached_stage_faces',0)
            if not isinstance(count,int) or not 0<=count<=60:
                raise ValueError('invalid cached stage face count')
            if count and counters.get('native_command_frames',0)<1:
                raise ValueError('cached stage has no completed native frame')
            if count:
                states[i]=dict(states[i])
                for field in ('faces_submitted','faces_flushed'):
                    states[i][field]+=count
            cached_faces[i]=count
    for state in states:
        if state['first_error'] or state['draw_timeouts']:
            raise ValueError('unhealthy guest capture')
        if not state['sim_ticks']:
            raise ValueError('capture has no committed ticks')
    for key in ('bios_sha256', 'probe_sha256'):
        if observations[0]['provenance'][key] != observations[1]['provenance'][key]:
            raise ValueError('different '+key)
    emulators = [json.loads((p/'ymir.json').read_text()) for p in (baseline, candidate)]
    for key in BOOT_FIELDS:
        if emulators[0]['boot'][key] != emulators[1]['boot'][key]:
            raise ValueError('different emulator '+key)
    if emulators[0]['division_timing_model'] != emulators[1]['division_timing_model']:
        raise ValueError('different division timing model')
    stable = [{k: v for k, v in s.items() if k not in VARIABLE_FIELDS} for s in states]
    if stable[0] != stable[1]:
        keys = sorted(set(stable[0]) | set(stable[1]))
        different = [k for k in keys if stable[0].get(k) != stable[1].get(k)]
        raise ValueError('different snapshot fields: '+', '.join(different))
    images = [Image.open(p/'screen.png').convert('RGB') for p in (baseline, candidate)]
    if images[0].size != (320, 224) or images[1].size != images[0].size:
        raise ValueError('expected matching 320x224 captures')
    crops = [im.crop(MODEL_CROP) for im in images]
    if ImageChops.difference(*crops).getbbox():
        raise ValueError('model-region pixels differ')
    return dict(passed=True, sim_ticks=states[0]['sim_ticks'],
                resident_texture_vram_bytes=resident_bytes,
                reconciled_cached_stage_faces=cached_faces,
                checked_fields=len(stable[0]), crop=MODEL_CROP,
                rgb_sha256=hashlib.sha256(crops[0].tobytes()).hexdigest(),
                baseline_elf=observations[0]['elf_sha256'],
                candidate_elf=observations[1]['elf_sha256'],
                scope='Matched frozen tick, guest state and exact model pixels; not a continuous state oracle or timing comparison.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--include-cached-stage-faces',action='store_true',
                        help='reconcile published native stage replay counts with world face totals')
    args = parser.parse_args()
    print(json.dumps(compare(args.baseline, args.candidate,args.include_cached_stage_faces), indent=2))
