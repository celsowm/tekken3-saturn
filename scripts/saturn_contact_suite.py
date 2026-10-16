#!/usr/bin/env python3
"""Verify exact contact/gate queries on owned Saturn poses; HP is not activated."""
import copy
import json
import saturn_probe


def execute(args):
    root=args.out.resolve();root.mkdir(parents=True,exist_ok=True);results={}
    cases=[('far-high','X','NONE','LEFT',False),
           ('near-high','X','NONE','RIGHT',True),
           ('near-mid','RIGHT+DOWN+X','NONE','RIGHT',True),
           ('near-low','DOWN+A','NONE','RIGHT',True),
           ('crouched-high','X','DOWN','RIGHT',False),
           ('crouched-low','DOWN+A','DOWN','RIGHT',True)]
    for name,attack,defense,movement,expected in cases:
        options=copy.copy(args);options.out=root/name
        options.out.mkdir(parents=True,exist_ok=True);options.frames=210;options.port2='pad'
        options.device_script=options.out/'input.devices'
        options.device_script.write_text(f'0 1 buttons=NONE\n0 2 buttons={defense}\n'
              f'20 1 buttons={movement}\n100 1 buttons=NONE\n120 1 buttons={attack}\n')
        result=saturn_probe.run(options);results[name]=result;s=result['state']
        checks=[s.get('psx_contact_active')==1,s['psx_owned_active']==1,
                (s['psx_contact_frames'][0]>0)==expected,
                (s['psx_gate_frames'][0]>0)==expected,s['psx_gate_frames'][1]==0,
                s['psx_health_q16']==[140*65536]*2,
                s['psx_attack_decode_errors']==[0,0],s['psx_skin_preview_applied']==[1,1],
                s['first_error']==0,s['draw_timeouts']==0]
        if not all(checks):
            (root/'failed.json').write_text(json.dumps(results,indent=2)+'\n')
            raise AssertionError(f'{name}: contact/gate query mismatch: {s}')
    report=dict(passed=True,hit_registration_enabled=False,damage_validated=False,cases=results)
    (root/'suite.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(results)} owned contact cases passed; {root / "suite.json"}')


if __name__=='__main__':
    parser=saturn_probe.parser()
    parser.set_defaults(build=saturn_probe.ROOT/'build/paul-owned',
                        out=saturn_probe.ROOT/'assets/imported/saturn-owned-contact-suite')
    execute(parser.parse_args())
