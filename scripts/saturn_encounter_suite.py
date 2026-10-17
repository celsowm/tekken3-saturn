#!/usr/bin/env python3
"""Record guards, a mid hit and lows through KO/replay/victory/next round.

Approach direction follows the previous observed side; no guest state is written.
Every iteration reruns its complete input prefix from reset. Requires an explicit
T3_CAPTURE_INPUT_TAPE build. Captures and emitted replay inputs stay private.
"""
import copy
import json
import saturn_probe


def execute(args):
    root=args.out.resolve();root.mkdir(parents=True,exist_ok=True)
    lines=['0 1 buttons=NONE','0 2 buttons=NONE','20 1 buttons=RIGHT','100 1 buttons=NONE',
           '120 1 buttons=X','200 1 buttons=NONE','230 2 buttons=DOWN','270 1 buttons=DOWN+A',
           '350 1 buttons=NONE','400 1 buttons=RIGHT','420 1 buttons=RIGHT+DOWN+X',
           '500 1 buttons=NONE','510 2 buttons=NONE']

    def run(name,frames):
        options=copy.copy(args);options.frames=frames;options.out=root/name
        options.out.mkdir(parents=True,exist_ok=True)
        options.device_script=options.out/'input.devices'
        options.device_script.write_text('\n'.join(lines)+'\n')
        result=saturn_probe.run(options);state=result['state']
        assert not state['faces_rejected'] and state['psx_attack_decode_errors']==[0,0]
        assert state.get('psx_round_flow_active')==1
        print(name,state['sim_ticks'],state['psx_registered_hits'],state['psx_registered_blocks'],
              state['psx_health_q16'],flush=True)
        return result

    result=run('standing-guard',200)
    assert result['state']['psx_registered_blocks']==[0,1]
    assert result['state']['psx_health_q16']==[140*65536]*2
    result=run('crouching-guard',350)
    assert result['state']['psx_registered_blocks']==[0,2]
    assert result['state']['psx_health_q16']==[140*65536]*2
    result=run('mid',600)
    assert result['state']['psx_registered_hits'][1]>=1
    for i in range(22):
        state=result['state'];tick=620+180*i
        direction='RIGHT' if state['fighter_x'][0]<state['fighter_x'][1] else 'LEFT'
        lines += [f'{tick} 1 buttons={direction}',f'{tick+60} 1 buttons=NONE',
                  f'{tick+68} 1 buttons=DOWN+A',f'{tick+138} 1 buttons=NONE']
        result=run(f'low-{i:02}',tick+175)
        state=result['state']
        if state['round_done'] and state['wins0']==1:
            assert state['psx_health_q16'][1]==0 and state['psx_ko_collapse'][1]
            print('KO',state['sim_ticks'],flush=True)
            ko_end=tick+175
            result=run('replay',ko_end+240)
            assert result['state']['psx_round_state']==4 and result['state']['psx_replay_mode'] in (1,3)
            for offset in range(300,1801,60):
                result=run('victory-search-'+str(offset),ko_end+offset)
                if result['state']['psx_round_state']==6:break
            else:raise AssertionError('no victory after original KO replay')
            result=run('victory',ko_end+offset)
            assert result['state']['psx_move_slot'][0] in range(3431,3435)
            result=run('next-round',ko_end+1600)
            assert result['state']['round']==2 and result['state']['wins0']==1
            assert not result['state']['round_done'] and result['state']['psx_round_state']==1
            assert result['state']['psx_health_q16']==[140*65536]*2
            (root/'passed.json').write_text(json.dumps(result,indent=2)+'\n')
            return result
    raise AssertionError('no KO within bounded encounter')


if __name__=='__main__':
    parser=saturn_probe.parser()
    parser.set_defaults(build=saturn_probe.ROOT/'build/paul-perf',
                        out=saturn_probe.ROOT/'assets/imported/saturn-encounter',capture_inputs=True)
    execute(parser.parse_args())
