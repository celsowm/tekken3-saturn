"""Identify a Saturn build and refuse mislabeled two-Paul release timings."""
import hashlib
import json
import re
from pathlib import Path

PROFILES = ('diagnostic', 'two-paul-release')
PAUL_SHAPE = dict(vertices=556, faces=773, joints=22, textures=724, blend_vertices=103)
SHAPE_MACROS = dict(vertices='T3_VERTEX_COUNT', faces='T3_FACE_COUNT',
                    joints='T3_JOINT_COUNT', textures='T3_TEXTURE_COUNT')
REQUIRED_ON = ('T3_ENABLE_PSX_OWNED_MATCH', 'T3_ENABLE_PSX_COMBAT',
               'T3_ENABLE_PSX_SKIN_PREVIEW', 'T3_FRAME_ON_SUBMIT', 'T3_CLOSE_CAMERA')
REQUIRED_OFF = ('T3_PERF_SAMPLES', 'T3_SIM_PROFILE', 'T3_POSE_TRACE',
                'T3_VERIFY_PARALLEL_GEOMETRY', 'T3_CPU_DIAGNOSTICS', 'T3_STAGE_HUD',
                'T3_CAPTURE_INPUT_TAPE', 'T3_REPLAY_INPUT_TAPE')
REQUIRED_OFF += ('T3_VERIFY_COMPACT_CAMERA_SKIN',)
REQUIRED_OFF += ('T3_VERIFY_COMPILED_RENDERER',)


def cache_entries(path):
    entries = {}
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith(('#', '//')) or ':' not in line or '=' not in line:
            continue
        key, value = line.split('=', 1)
        entries[key.split(':', 1)[0]] = value
    return entries


def inspect(build):
    build = Path(build)
    cache = cache_entries(build / 'CMakeCache.txt')
    header = (build / 'generated/characters/t3_generated_character.h').read_text()
    defines = dict(re.findall(r'^#define\s+(T3_\w+)\s+(\d+)u?\s*$', header, re.M))
    manifest = json.loads((build / 'generated/characters/manifest.json').read_text())
    shape = {key: int(defines[macro]) for key, macro in SHAPE_MACROS.items()}
    shape['blend_vertices'] = manifest.get('blend_vertices')
    original = defines.get('T3_LOCAL_KMD_BIND') == '1' and manifest.get('local_kmd_bind_preview') is True
    inputs = {}
    for name, value in cache.items():
        if name.startswith('T3_LOCAL_') and value and Path(value).is_file():
            inputs[name] = hashlib.sha256(Path(value).read_bytes()).hexdigest()
    stage=None
    if cache.get('T3_VDP2_FLOOR') in ('ON','1','TRUE'):
        stage_root=build/'generated/private_stage'
        stage=json.loads((stage_root/'manifest.json').read_text())
        digest=hashlib.sha256((stage_root/'PAUL.STG').read_bytes()).hexdigest()
        if digest!=stage.get('output_sha256'):
            raise ValueError('private stage asset does not match its manifest')
        stage=dict(name=stage['stage'],asset_sha256=digest,bytes=stage['output_bytes'],
                   panorama=stage['panorama'],floor=stage['floor'])
    sdk_archive = Path(cache.get('LibSaturn_DIR', '.')).parent.parent/'libsaturn.a'
    return dict(schema=1, kind='original-kmd' if original else 'procedural',
                shape=shape, original_textures=defines.get('T3_ORIGINAL_TEXTURES') == '1',
                archive_bytes=int(defines['T3_ARCHIVE_BYTES']),
                archive_sha256=manifest.get('archive_sha256'), input_sha256=inputs,
                sdk_archive_sha256=hashlib.sha256(sdk_archive.read_bytes()).hexdigest()
                    if sdk_archive.is_file() else None,
                configuration={k: v for k, v in cache.items() if k.startswith('T3_')},
                build_type=cache.get('CMAKE_BUILD_TYPE'),stage=stage)


def validate(workload, profile, state=None):
    if profile not in PROFILES:
        raise ValueError('unknown workload profile: ' + profile)
    if profile == 'diagnostic':
        return
    errors = []
    if workload['kind'] != 'original-kmd' or not workload['original_textures']:
        errors.append('original KMD mesh and textures are required')
    if workload['shape'] != PAUL_SHAPE:
        errors.append('expected the complete Paul shape ' + str(PAUL_SHAPE))
    config = workload['configuration']
    if workload['build_type'] != 'Release':
        errors.append('Release build is required')
    for name in REQUIRED_ON:
        if config.get(name) not in ('ON', '1', 'TRUE'):
            errors.append(name + ' must be ON')
    for name in REQUIRED_OFF:
        if config.get(name, 'OFF') not in ('OFF', '0', 'FALSE', ''):
            errors.append(name + ' must be OFF')
    for name in ('T3_PERF_MODE', 'T3_RENDER_BENCH_TICK'):
        if config.get(name, '0') != '0':
            errors.append(name + ' must be 0')
    if not workload.get('archive_sha256') or not workload['input_sha256'].get('T3_LOCAL_KMD'):
        errors.append('private asset hashes are required')
    if state is not None:
        for name in ('sim_ticks','observed_fields'):
            if state.get(name,0)<1:
                errors.append('guest '+name+' must advance; submissions alone do not establish gameplay')
        for name in ('psx_owned_active', 'psx_combat_active', 'psx_round_flow_active'):
            if state.get(name) != 1:
                errors.append('guest ' + name + ' must be active')
        for name in ('psx_skin_preview_applied', 'psx_pose_status'):
            if state.get(name) != [1, 1]:
                errors.append('guest ' + name + ' must confirm both actors')
        if state.get('model_texture_count') != PAUL_SHAPE['textures']:
            errors.append('guest texture count does not match Paul')
        if state.get('model_archive_bytes') != workload['archive_bytes']:
            errors.append('guest archive does not match the build')
    if errors:
        raise ValueError('two-paul-release workload rejected: ' + '; '.join(errors))


def validate_optimizations(workload,profile,counters):
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_ASSET_TOPOLOGY') in ('ON','1','TRUE'):
        if counters.get('asset_topology_ready',0)!=1:
            raise ValueError('two-paul-release rejected: resident asset topology was not bound')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_SLAVE_SIMULATION') in ('ON','1','TRUE'):
        if min(counters.get('slave_sim_'+name,0) for name in ('frames','jobs','ticks','pose_tick'))<1:
            raise ValueError('two-paul-release rejected: selected Slave simulation never completed')
        if not 0<=counters.get('slave_sim_pose_lag',-1)<=2 or \
                not 0<counters.get('slave_sim_stack_used',0)<=16384-256:
            raise ValueError('two-paul-release rejected: invalid Slave simulation lag or stack use')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_SHARE_ACTOR_RESOURCES') in ('ON','1','TRUE'):
        if counters.get('shared_actor_resources',0)!=1:
            raise ValueError('two-paul-release rejected: selected shared actor resources never aliased identical assets')
    if profile == 'two-paul-release' and int(workload['configuration'].get(
            'T3_WORLD_POSE_CACHE_ENTRIES','0'))>0:
        if counters.get('world_cache_hits',0)<1:
            raise ValueError('two-paul-release rejected: selected world composition cache never reused a pose')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_BRANCH_INPUT_FILTER') in ('ON','1','TRUE'):
        if min(counters.get(name,0) for name in (
                'branch_filter_ready','branch_filter_used','branch_filter_words'))<1:
            raise ValueError('two-paul-release rejected: selected branch filter never prepared and skipped rows')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_FUSED_ACTOR_PREPARATION') in ('ON','1','TRUE'):
        if counters.get('fused_actor_frames',0)<1:
            raise ValueError('two-paul-release rejected: selected fused actor path never executed')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_VDP2_FLOOR') in ('ON','1','TRUE'):
        if min(counters.get(name,0) for name in (
                'vdp2_floor_prepared','vdp2_floor_presented','vdp2_floor_updates'))<1:
            raise ValueError('two-paul-release rejected: selected VDP2 floor never completed')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_OVERLAP_VDP1') in ('ON','1','TRUE'):
        if counters.get('vdp1_overlap_frames',0)<1:
            raise ValueError('two-paul-release rejected: selected VDP1 overlap never completed a frame')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_COMPACT_CAMERA_SKIN') in ('ON','1','TRUE'):
        if counters.get('camera_skin_frames',0)<1:
            raise ValueError('two-paul-release rejected: selected camera skin path never completed a frame')

    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_DIRECT_FACE_SPANS') in ('ON','1','TRUE'):
        if counters.get('geometry_pairs',0)<1:
            raise ValueError('two-paul-release rejected: selected direct worker path never completed a pair')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_COMPILED_RENDERER') in ('ON','1','TRUE'):
        if counters.get('compiled_skin_frames',0)<1:
            raise ValueError('two-paul-release rejected: selected compiled renderer never completed a frame')
    if profile == 'two-paul-release' and workload['configuration'].get(
            'T3_NATIVE_COMMANDS') in ('ON','1','TRUE'):
        if counters.get('native_command_frames',0)<1:
            raise ValueError('two-paul-release rejected: selected native command batch never completed a frame')
