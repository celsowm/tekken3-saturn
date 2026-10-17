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

def decode(raw):
    if len(raw) < WORDS * 4:
        raise ValueError('truncated diagnostics')
    values = list(struct.unpack_from('>' + 'I' * WORDS, raw))
    result = dict(zip(PREFIX, values[:len(PREFIX)]))
    if result['magic'] != 0x54334431 or result['version'] not in (12,13,14,15,16,17,18,19):
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
    for name, kind, count in extended:
        alignment = struct.calcsize('>' + kind)
        at = (at + alignment - 1) // alignment * alignment
        if at + alignment * count > len(raw):
            raise ValueError('truncated PSX diagnostics')
        data = list(struct.unpack_from('>' + kind * count, raw, at))
        result[name] = data[0] if count == 1 else data
        at += alignment * count
    if result['snapshot_epoch'] & 1:
        raise ValueError('emulator stopped during diagnostic publication; choose a later checkpoint')
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


def run(args):
    build, out = args.build.resolve(), args.out.resolve()
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
    if args.profile:
        # Preserve symbols beside sampled PCs before a later rebuild moves them.
        # These private ELF/map artifacts must remain outside Git.
        shutil.copyfile(elf,out/'profile.elf')
        map_path=elf.with_suffix('.elf.map')
        if map_path.exists():shutil.copyfile(map_path,out/'profile.map')
        for kind in ('pc', 'cycles', 'instructions', 'transfers'):
            command.extend(['--profile-' + kind, str(out / (kind + '.csv'))])
    with (out / 'probe.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True,
                       timeout=args.timeout)
    report = json.loads((out / 'ymir.json').read_text())
    boot = report['boot']
    if not boot.get('injected') or not start <= boot['pc_after_run'] < start + binary.stat().st_size:
        raise ValueError('probe did not finish executing inside application')
    state = decode((out / 'diagnostics.bin').read_bytes())
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
                  diagnostics_address=address, provenance=provenance, command=command, state=state)
    (out / 'observation.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build/saturn')
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
    return p


if __name__ == '__main__':
    options = parser().parse_args()
    if options.frames < 1:
        raise SystemExit('--frames must be positive')
    print(json.dumps(run(options), indent=2))
