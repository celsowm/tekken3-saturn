#!/usr/bin/env python3
"""Run the external Ymir harness and decode the matching SH-2 diagnostics.

No emulator or retail data is bundled. A run certifies injected execution,
not CD boot. All outputs, including captures, should stay in an ignored path.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import saturn_workload

ROOT = Path(__file__).resolve().parents[1]
PREFIX = ('magic version frame_serial vblanks sim_ticks sim_steps_last_frame '
          'loop_rate_x10 frame_work_ms faces_submitted faces_flushed '
          'faces_culled faces_rejected commands_used commands_capacity '
          'late_swaps draw_waits draw_timeouts first_error').split()
PAIRS = ('pad_connected pad_held pad_pressed fighter_x fighter_z health '
         'attack attack_age guarding crouching stun').split()
TAIL = 'round wins0 wins1 round_done match_done winner timer hits blocks'.split()
WORDS = len(PREFIX) + 2 * len(PAIRS) + len(TAIL)
PSX_FIELDS = [
    ('psx_logic_active', 'I', 1), ('psx_move_slot', 'H', 2),
    ('psx_move_frame', 'H', 2), ('psx_attack_word', 'H', 2),
    ('psx_transitions', 'I', 2), ('psx_attack_segments', 'B', 2),
    ('psx_attack_decode_errors', 'I', 2), ('psx_pose_status', 'B', 2),
    ('psx_pose_frames', 'B', 2), ('psx_motion_root', 'h', 6),
    ('psx_fk_valid_slots', 'I', 2), ('psx_fk_valid_model_parts', 'I', 2),
    ('psx_ik_valid_slots', 'I', 2), ('psx_skin_preview_applied', 'B', 2),
    ('psx_hurt_zones_ready', 'B', 2), ('psx_hurt_head_local_y', 'i', 2),
    ('psx_fk_root_xx', 'h', 2), ('psx_shadow_contact_status', 'B', 2),
    ('psx_shadow_contact_zone', 'b', 2), ('psx_shadow_contact_edges', 'I', 2),
    ('psx_shadow_overlap_frames', 'I', 2), ('psx_event_status', 'B', 2),
    ('psx_extra_kind', 'B', 2), ('psx_extra_damage', 'B', 2),
    ('psx_events_fired', 'H', 2),
    ('snapshot_epoch', 'I', 1),
]
ASSET_FIELDS = [(name,'I',1) for name in (
    'model_archive_bytes','model_texture_count','model_texture_vram_bytes',
    'skin_work_ms','render_work_ms','submitted_frames')]
LIFECYCLE_FIELDS=[('psx_lifecycle_active','I',1)]+[(name,'h',2) for name in (
    'psx_pending_slot','psx_root_frame','psx_move_age','psx_freeze','psx_hold_frames')]

OWNED_FIELDS=[('psx_owned_active','I',1),('psx_health_q16','i',2),
              ('psx_world_root','i',6),('psx_anchor','i',6)]

BODY_FIELDS=[('psx_body_active','I',1),('psx_body_contacts','I',2),('psx_body_push','i',6)]

CONTACT_FIELDS=[('psx_contact_active','I',1),('psx_geometry_status','I',2),
                ('psx_contact_zone','i',2),('psx_hit_gate','I',2),('psx_contact_frames','I',2)]

COMBAT_FIELDS=[('psx_combat_active','I',1)]+[(name,kind,2) for name,kind in (
    ('psx_registered_hits','I'),('psx_registered_blocks','I'),('psx_unsupported_moves','I'),
    ('psx_damage_last_tick','i'),('psx_reaction_slot','i'),('psx_reaction_transition','i'),
    ('psx_hit_done','I'),('psx_guarded_last_tick','I'),('psx_ko_collapse','I'))]
ROUND_FIELDS=[('psx_round_flow_active','I',1)]+[(name,'i',1) for name in (
    'psx_round_state','psx_round_counter','psx_round_hold','psx_replay_mode',
    'psx_replay_record','psx_replay_play','psx_replay_mark')]+[(name,'I',1) for name in (
    'psx_replay_recorded','psx_replay_epoch')]
SCHEDULE_FIELDS=[(name,'I',1) for name in (
    'elapsed_fields_last','observed_fields','discarded_fields','scheduled_ticks')]

class IncompleteSnapshotError(ValueError):
    """Guest was stopped while publishing its diagnostic snapshot."""


def decode(raw):
    if len(raw) < WORDS * 4:
        raise ValueError('truncated diagnostics')
    values = list(struct.unpack_from('>' + 'I' * WORDS, raw))
    result = dict(zip(PREFIX, values[:len(PREFIX)]))
    if result['magic'] != 0x54334431 or result['version'] not in range(12,23):
        raise ValueError('unsupported diagnostics magic/version')
    at = len(PREFIX)
    for name in PAIRS:
        result[name] = values[at:at + 2]
        at += 2
    result.update(zip(TAIL, values[at:]))
    for name in ('fighter_x', 'fighter_z'):
        result[name] = [x if x < 0x80000000 else x - 0x100000000
                        for x in result[name]]
    for name in ('winner', 'first_error'):
        if result[name] >= 0x80000000:
            result[name] -= 0x100000000
    if not result['frame_serial']:
        raise ValueError('guest has not published a gameplay frame')
    at = WORDS * 4
    extended=PSX_FIELDS+(ASSET_FIELDS if result['version']>=13 else [])
    extended+=LIFECYCLE_FIELDS if result['version']>=14 else []
    extended+=OWNED_FIELDS if result['version']>=15 else []
    extended+=BODY_FIELDS if result['version']>=16 else []
    extended+=CONTACT_FIELDS if result['version']>=17 else []
    extended+=[('psx_gate_frames','I',2)] if result['version']>=18 else []
    extended+=COMBAT_FIELDS if result['version']>=19 else []
    extended+=ROUND_FIELDS if result['version']>=20 else []
    extended+=SCHEDULE_FIELDS if result['version']>=21 else []
    extended+=[('local_pose_cache','I',4)] if result['version']>=22 else []
    for name, kind, count in extended:
        alignment = struct.calcsize('>' + kind)
        at = (at + alignment - 1) // alignment * alignment
        if at + alignment * count > len(raw):
            raise ValueError('truncated PSX diagnostics')
        data = list(struct.unpack_from('>' + kind * count, raw, at))
        result[name] = data[0] if count == 1 else data
        at += alignment * count
    if result['snapshot_epoch'] & 1:
        raise IncompleteSnapshotError('emulator stopped during diagnostic publication; choose a later checkpoint')
    return result


def symbols(elf, nm):
    output = subprocess.check_output([nm, '-S', '--defined-only', str(elf)], text=True)
    result = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 4:
            result[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
        elif len(parts) == 3:
            result[parts[2]] = (int(parts[0], 16), 0)
    return result


def decode_panic(raw):
    if len(raw)!=44:raise ValueError('truncated native panic record')
    magic,code,line,filename=struct.unpack('>Iii32s',raw)
    if magic!=0x50414E43:return None
    return dict(code=code,line=line,file=filename.split(b'\0',1)[0].decode('utf-8',errors='replace'))

def run(args):
    build, out = args.build.resolve(), args.out.resolve()
    workload = saturn_workload.inspect(build)
    workload_profile = getattr(args, 'workload', 'diagnostic')
    saturn_workload.validate(workload, workload_profile)
    if workload_profile == 'two-paul-release' and (args.profile or getattr(args, 'debug_profile', False)):
        raise ValueError('two-paul-release cadence excludes intrusive CPU profiling; use diagnostic')
    out.mkdir(parents=True, exist_ok=True)
    elf = build / 'tekken3_saturn.elf'
    binary = build / 'tekken3_saturn.app.bin'
    syms = symbols(elf, args.nm)
    address, size = syms['_g_t3_diagnostics']
    start = syms['_start'][0]
    probe = args.probe or args.libsaturn / 'harness/build-macos/probe'
    command = [str(probe.resolve()), '--iso', str(build / 'disc/tekken3_saturn.iso'),
               '--bin', str(binary), '--bios', str(args.bios.resolve()),
               '--boot-frames', '90', '--frames', str(args.frames),
               '--out', str(out / 'ymir.json'),
               '--dump-mem', f'{address:#x}:{size}:{out / "diagnostics.bin"}',
               '--screenshot', f'{args.frames - 1}:{out / "screen.png"}',
               '--port-device', '1:pad', '--port-device', f'2:{args.port2}']
    for field in getattr(args,'screenshot_frame',[]):
        if not 0<=field<args.frames:raise ValueError('screenshot frame must be inside the program run')
        command.extend(['--screenshot',f'{field}:{out / ("frame-"+str(field)+".png")}'])
    if getattr(args,'capture_inputs',False):
        for name,filename in [('_g_t3_input_tape_count','input-count.bin'),
                              ('_g_t3_input_tape_overflow','input-overflow.bin'),
                              ('_g_t3_input_tape','input-tape.bin')]:
            if name not in syms:
                raise ValueError('input recording requires a T3_CAPTURE_INPUT_TAPE=ON build')
            location,length=syms[name]
            command.extend(['--dump-mem',f'{location:#x}:{length}:{out / filename}'])
    if args.device_script:
        command.extend(['--device-script', str(args.device_script.resolve())])
    if getattr(args,'wram_low_fill',None) is not None:
        if not 0<=args.wram_low_fill<=255:raise ValueError('Low WRAM fill must be a byte')
        command.extend(['--wram-low-fill',str(args.wram_low_fill)])
    if getattr(args,'sh2_cache',None) is not None:
        command.extend(['--sh2-cache',args.sh2_cache])
    if getattr(args,'video_crop',None) and not getattr(args,'video_observations',False):
        raise ValueError('--video-crop requires --video-observations')
    if getattr(args,'video_observations',False):
        command.extend(['--video-observations',str(out/'video.csv')])
        if getattr(args,'video_crop',None):command.extend(['--video-crop',args.video_crop])
    if getattr(args,'debug_profile',False):
        command.extend(['--debug-profile',str(out/'debug'),
                        '--trace-from',str(args.trace_from),
                        '--trace-frames',str(args.trace_frames),
                        '--trace-capacity',str(args.trace_capacity)])
        for name in args.snapshot_symbol:
            symbol=name if name in syms else '_'+name
            if symbol not in syms:raise ValueError('missing snapshot symbol: '+name)
            command.extend(['--snapshot-pc',hex(syms[symbol][0])])
    dump_requests=list(getattr(args,'symbol_dumps',[]))
    if '_g_sat_last_panic' in syms:
        dump_requests.append(('_g_sat_last_panic','native-panic.bin'))
    for i,name in enumerate(getattr(args,'dump_symbol',[])):
        symbol=name if name in syms else '_'+name
        dump_requests.append((symbol,'symbol-'+str(i)+'.bin'))
    dump_manifest={}
    for symbol,filename in dump_requests:
        if symbol not in syms:raise ValueError('missing dump symbol: '+symbol)
        location,length=syms[symbol]
        if not length:raise ValueError('dump symbol has no size: '+symbol)
        command.extend(['--dump-mem',f'{location:#x}:{length}:{out / filename}'])
        dump_manifest[symbol]=dict(file=filename,address=location,bytes=length,byte_order='big')
    if dump_requests:
        shutil.copyfile(build/'CMakeCache.txt',out/'capture.CMakeCache.txt')
    if args.profile or getattr(args,'debug_profile',False):
        # Preserve symbols beside sampled PCs before a later rebuild moves them.
        # These private ELF/map artifacts must remain outside Git.
        shutil.copyfile(elf,out/'profile.elf')
        map_path=elf.with_suffix('.elf.map')
        if map_path.exists():shutil.copyfile(map_path,out/'profile.map')
        shutil.copyfile(build/'CMakeCache.txt',out/'profile.CMakeCache.txt')
        if args.profile:
            for kind in ('pc', 'cycles', 'instructions', 'transfers'):
                command.extend(['--profile-' + kind, str(out / (kind + '.csv'))])
    with (out / 'probe.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True,
                       timeout=args.timeout)
    report = json.loads((out / 'ymir.json').read_text())
    if dump_manifest:(out/'symbol-dumps.json').write_text(json.dumps(dump_manifest,indent=2)+'\n')
    if (out/'native-panic.bin').exists():
        panic=decode_panic((out/'native-panic.bin').read_bytes())
        if panic:
            (out/'native-panic.json').write_text(json.dumps(panic,indent=2)+'\n')
            raise ValueError(f'native guest panic: {panic}')
    boot = report['boot']
    if not boot.get('injected') or not start <= boot['pc_after_run'] < start + binary.stat().st_size:
        raise ValueError('probe did not finish executing inside application')
    state = decode((out / 'diagnostics.bin').read_bytes())
    saturn_workload.validate(workload, workload_profile, state)
    if state['first_error'] or state['draw_timeouts'] or state['commands_used'] > state['commands_capacity']:
        raise ValueError(f'guest rendering failed: {state}')
    if getattr(args,'capture_inputs',False):
        count=struct.unpack('>I',(out/'input-count.bin').read_bytes())[0]
        overflow=struct.unpack('>I',(out/'input-overflow.bin').read_bytes())[0]
        if overflow or count<1 or count>512:raise ValueError('input trace is empty or overflowed')
        raw=(out/'input-tape.bin').read_bytes()
        events=[list(struct.unpack_from('>5I',raw,i*20)) for i in range(count)]
        (out/'input-tape.json').write_text(json.dumps(dict(events=events,
            final_tick=state['sim_ticks'],scope='Captured relative simulation inputs; no guest state injection'),indent=2)+'\n')
    provenance = {}
    for name, path in [('app', binary), ('iso', build / 'disc/tekken3_saturn.iso'),
                       ('probe', probe), ('bios', args.bios)]:
        provenance[name + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name, path in [('project', ROOT), ('libsaturn', args.libsaturn),
                       ('research', ROOT / 'third_party/Tekken3Remake')]:
        revision = subprocess.run(['git', '-C', str(path), 'describe', '--always', '--dirty'],
                                  capture_output=True, text=True)
        provenance[name + '_revision'] = revision.stdout.strip() if revision.returncode == 0 else None
    result = dict(emulator_frames=args.frames, cd_boot_validated=False,
                  elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                  diagnostics_address=address, provenance=provenance, command=command, state=state,
                  workload_profile=workload_profile, workload=workload)
    (out / 'observation.json').write_text(json.dumps(result, indent=2) + '\n')
    if getattr(args,'debug_profile',False):
        import analyze_saturn_profile
        analysis=analyze_saturn_profile.analyze(out/'debug',out/'profile.elf',args.nm)
        (out/'profile-summary.json').write_text(json.dumps(analysis,indent=2)+'\n')
        if args.snapshot_symbol:
            analyze_saturn_profile.disassemble_snapshots(out/'debug',
                Path(args.nm).with_name('sh2eb-elf-objdump'))
    return result


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build/saturn')
    p.add_argument('--workload', choices=saturn_workload.PROFILES, default='diagnostic',
                   help='two-paul-release refuses procedural, frozen or instrumented timing builds')
    p.add_argument('--libsaturn', type=Path, default=ROOT.parent / 'lib-saturn')
    p.add_argument('--probe', type=Path)
    p.add_argument('--bios', type=Path, default=os.environ.get('LIBSATURN_BIOS'),
                   required=not os.environ.get('LIBSATURN_BIOS'))
    p.add_argument('--nm', default=shutil.which('sh2eb-elf-nm') or
                   str(ROOT.parent / 'saturn-tools/bin/sh2eb-elf-nm'))
    p.add_argument('--out', type=Path, default=ROOT / 'assets/imported/saturn-probe')
    p.add_argument('--frames', type=int, default=180)
    p.add_argument('--timeout', type=int, default=120)
    p.add_argument('--port2', choices=('pad', 'none'), default='pad')
    p.add_argument('--device-script', type=Path)
    p.add_argument('--capture-inputs',action='store_true',help='dump an explicitly instrumented input recording build')
    p.add_argument('--profile', action='store_true', help='capture PC, cycle, instruction and transfer profiles')
    p.add_argument('--wram-low-fill',type=lambda s:int(s,0),help='injected boot with Low WRAM initialized to this byte')
    p.add_argument('--sh2-cache',choices=('on','off'),help='explicit Ymir cache emulation; separate from guest CCR')
    p.add_argument('--video-observations',action='store_true',help='record exact composited RGB changes per field; requires updated external harness')
    p.add_argument('--video-crop',help='X:Y:WIDTH:HEIGHT region for video observations, excluding HUD if desired')
    p.add_argument('--screenshot-frame',action='append',type=int,default=[],help='retain an additional screenshot at this program field')
    p.add_argument('--debug-profile',action='store_true',help='bounded per-CPU execution costs, events and registers')
    p.add_argument('--trace-from',type=int,default=0)
    p.add_argument('--trace-frames',type=int,default=30)
    p.add_argument('--trace-capacity',type=int,default=262144)
    p.add_argument('--snapshot-symbol',action='append',default=[],help='resolve an exact PC checkpoint from this ELF')
    p.add_argument('--dump-symbol',action='append',default=[],help='dump a variable using this ELF; write a byte-order/address manifest')
    return p


if __name__ == '__main__':
    options = parser().parse_args()
    if options.frames < 1:
        raise SystemExit('--frames must be positive')
    print(json.dumps(run(options), indent=2))
