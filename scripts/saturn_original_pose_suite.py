#!/usr/bin/env python3
"""Verify private authored move/pose control through the real Saturn frontend.

This checks the visual MoveVM observer, not authoritative original combat HP.
"""
import copy
import json

import saturn_probe


def execute(args):
    root = args.out.resolve()
    results = {}
    cases=[
        ('idle', 'NONE', 'NONE', [3, 3]),
        ('left-punch', 'X', 'NONE', [295, 3]),
        ('right-punch', 'Y', 'NONE', [296, 3]),
        ('left-kick', 'A', 'NONE', [300, 3]),
        ('right-kick', 'B', 'NONE', [301, 3]),
        ('independent-actors', 'X', 'B', [295, 301]),
    ]
    if args.require_phased:
        cases += [('mid-command','RIGHT+DOWN+X','NONE',[357,3]),
                  ('low-command','DOWN+A','NONE',[355,3]),
                  ('low-recovery','DOWN+A','NONE',[3564,3])]
    for name,p1,p2,slots in cases:
        options = copy.copy(args)
        options.out = root / name
        options.out.mkdir(parents=True, exist_ok=True)
        # The captured low ends at pose 16 and continues into slot 3564.
        # Observe both the active attack and the later authored recovery.
        options.frames, options.port2 = (90 if name=='low-command' else 120), 'pad'
        options.device_script = options.out / 'input.devices'
        options.device_script.write_text(
            f'0 1 buttons=NONE\n0 2 buttons=NONE\n60 1 buttons={p1}\n60 2 buttons={p2}\n')
        result = saturn_probe.run(options)
        state = result['state']
        results[name] = result
        checks = (state['psx_move_slot'] == slots,
                  state['psx_logic_active'] == 1,
                  state['psx_pose_status'] == [1, 1],
                  state['psx_skin_preview_applied'] == [1, 1],
                  state['psx_fk_valid_model_parts'] == [0x3ffff, 0x3ffff],
                  state['psx_attack_decode_errors'] == [0, 0],
                  not args.require_phased or state.get('psx_lifecycle_active')==1,
                  not args.require_owned or state.get('psx_owned_active')==1)
        if not all(checks):
            (root / 'failed.json').write_text(json.dumps(results, indent=2))
            raise AssertionError(f'{name}: incomplete or wrong authored move/pose: {state}')
    (root / 'suite.json').write_text(json.dumps(dict(passed=True, original_damage_validated=False,
                                                   cases=results), indent=2) + '\n')
    print(f'{len(results)} authored-pose emulator cases passed; {root / "suite.json"}')


if __name__ == '__main__':
    parser = saturn_probe.parser()
    parser.add_argument('--require-phased',action='store_true',
                        help='Require original begin/queue lifecycle and original mid/low commands')
    parser.add_argument('--require-owned',action='store_true',
                        help='Require the authoritative original movement mode')
    parser.set_defaults(build=saturn_probe.ROOT / 'build/paul',
                        out=saturn_probe.ROOT / 'assets/imported/saturn-original-pose-suite')
    execute(parser.parse_args())
