#!/usr/bin/env python3
"""Exercise owned original movement in Ymir; contact/damage are not certified."""
import copy
import json
import saturn_probe


def execute(args):
    root=args.out.resolve();root.mkdir(parents=True,exist_ok=True)
    results={}
    def case(name,script='',frames=180,port2='pad'):
        options=copy.copy(args);options.out=root/name
        options.out.mkdir(parents=True,exist_ok=True)
        options.frames=frames;options.port2=port2
        options.device_script=options.out/'input.devices'
        options.device_script.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n'+script)
        result=saturn_probe.run(options);results[name]=result
        state=result['state']
        require(state.get('psx_owned_active')==1,'owned mode must be enabled')
        require(state['psx_health_q16']==[140*65536]*2,'movement must not invent damage')
        require(state['psx_attack_decode_errors']==[0,0] and not state['first_error']
                and not state['draw_timeouts'],'runtime/decoder error')
        require(state['psx_skin_preview_applied']==[1,1],'both original actors must render')
        return state
    def require(ok,message):
        if not ok:
            (root/'failed.json').write_text(json.dumps(results,indent=2)+'\n')
            raise AssertionError(message)
    neutral=case('neutral')
    require(neutral['psx_anchor'][::3]==[-1000,1000],'neutral anchors must remain at spawn X')
    require(neutral==case('neutral-repeat'),'identical runs must reproduce diagnostics')
    approach=case('approach','20 1 buttons=RIGHT\n60 1 buttons=NONE\n')
    require(approach['psx_world_root'][0]>-1000 and
            approach['psx_world_root'][0]<approach['psx_world_root'][3],
            'P1 approach must move its original root independently')
    if approach.get('psx_body_active'):
        require(sum(approach['psx_body_contacts'])>0,'approach must exercise body separation')
        for frames in (240,360,480):
            close=case(f'body-pressure-{frames}','20 1 buttons=RIGHT\n',frames)
            require(close['psx_world_root'][0]<close['psx_world_root'][3] and
                    sum(close['psx_body_contacts'])>0,'continued approach must separate without crossing')
    retreat=case('retreat' ,'20 1 buttons=LEFT\n60 1 buttons=NONE\n')
    require(retreat['psx_world_root'][0]<-1000,'P1 retreat must move away')
    second=case('second-pad','20 2 buttons=LEFT\n60 2 buttons=NONE\n')
    require(second['psx_world_root'][3]<1000 and
            second['psx_world_root'][0]<second['psx_world_root'][3],
            'P2 must independently approach')
    disconnected=case('disconnected-p2',port2='none')
    require(disconnected['pad_connected'][1]==0 and disconnected['psx_anchor'][3]==1000,
            'disconnected P2 must remain neutral without prototype CPU')
    held=case('held-punch','60 1 buttons=X\n',300)
    released=case('repressed-punch','60 1 buttons=X\n130 1 buttons=NONE\n170 1 buttons=X\n',300)
    require(released['psx_transitions'][0]>held['psx_transitions'][0],
            'release/repress must produce another transition compared with held punch')
    reset=case('reset','20 1 buttons=RIGHT\n60 1 buttons=NONE\n200 1 buttons=START\n220 1 buttons=NONE\n',300)
    require(reset['psx_anchor'][::3]==[-1000,1000] and reset['sim_ticks']<neutral['sim_ticks'],
            'START must reset owned anchors and tick counter')
    (root/'suite.json').write_text(json.dumps(dict(passed=True,original_contact_validated=False,
                                                   cases=results),indent=2)+'\n')
    print(f'{len(results)} owned movement cases passed; {root / "suite.json"}')


if __name__=='__main__':
    parser=saturn_probe.parser()
    parser.set_defaults(build=saturn_probe.ROOT/'build/paul-owned',
                        out=saturn_probe.ROOT/'assets/imported/saturn-owned-input-suite')
    execute(parser.parse_args())
