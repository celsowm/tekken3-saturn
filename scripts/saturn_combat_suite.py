#!/usr/bin/env python3
"""Run original damage/guard/reaction/round/replay cases in headless Ymir.

These are Saturn behavior regressions, not aligned PS1 input-replay parity.
"""
import copy
import json
import shutil
import saturn_probe


def delayed_script(script, fields):
    if fields < 0:
        raise ValueError('input offset must be nonnegative')
    return ''.join(str(int(line.split(None,1)[0])+fields)+' '+line.split(None,1)[1]+'\n'
                   for line in script.splitlines() if line.strip())


# The world-composition cache check needs several hundred ticks of live play
# in a 300-field case, and cache reuse is counted per submitted image, so it
# depends on throughput. Starting every window this many fields after the
# first tick is an empirical margin: 16 fields failed the high-hit case, 50
# passed every case on both the cold-Low-WRAM candidate and its control; it is
# not a minimum and not a model.
GAMEPLAY_LEAD_FIELDS=50


def first_gameplay_field(args,runner=saturn_probe.run,limit=1200,resolution=10):
    """Return the first program field at which the simulation has ticked.

    Boot-time CD loads vary by build, so fixed field windows are measured from
    this point instead. Probes use neutral pads in their own directories.
    """
    def ticked(fields):
        a=copy.copy(args);a.out=args.out/f'gameplay-start-{fields}';a.out.mkdir(parents=True,exist_ok=True)
        a.frames=fields;a.port2='pad';a.workload='diagnostic';a.device_script=a.out/'input.devices'
        a.device_script.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n')
        try:
            return coherent_case(a,runner)['state']['sim_ticks']>0
        except ValueError as error:
            if any(text in str(error) for text in ('not published a gameplay frame','unsupported diagnostics magic')):return False
            raise
    high=resolution
    while not ticked(high):
        if high>=limit:raise AssertionError('simulation never started within %d fields'%limit)
        high=min(limit,high*2)
    low=high//2 if high>resolution else 0
    while high-low>resolution:
        mid=(low+high)//2
        if ticked(mid):high=mid
        else:low=mid
    return high


def coherent_case(args,runner=saturn_probe.run):
    """Retry only torn debugger publications, retaining the rejected artifacts.

    Run assertions still inspect the actual later endpoint; gameplay failures,
    native panic, malformed diagnostics and process errors are never retried.
    """
    requested=args.frames
    for extra in range(3):
        attempt=copy.copy(args);attempt.frames=requested+extra
        try:
            result=runner(attempt)
        except saturn_probe.IncompleteSnapshotError:
            archive=args.out/f'incomplete-{attempt.frames}'
            archive.mkdir()
            for path in list(args.out.iterdir()):
                if path.is_file():
                    if path==args.device_script:shutil.copyfile(path,archive/path.name)
                    else:shutil.move(str(path),archive/path.name)
            if extra==2:raise
            continue
        result['requested_emulator_frames']=requested
        result['publication_retry_fields']=extra
        (args.out/'observation.json').write_text(json.dumps(result,indent=2)+'\n')
        return result


def execute(args):
    root=args.out.resolve();root.mkdir(parents=True,exist_ok=True);results={}
    def require(ok,message):
        if not ok:
            (root/'failed.json').write_text(json.dumps(results,indent=2)+'\n')
            raise AssertionError(message)
    offset=getattr(args,'input_offset_fields',None)
    measured=offset is None
    if measured:offset=first_gameplay_field(args)+GAMEPLAY_LEAD_FIELDS
    def case(name,script='',frames=300):
        a=copy.copy(args);a.out=root/name;a.out.mkdir(parents=True,exist_ok=True)
        a.frames=frames+offset;a.port2='pad';a.device_script=a.out/'input.devices'
        a.device_script.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n'+delayed_script(script,offset))
        r=coherent_case(a);results[name]=r;s=r['state']
        require(s.get('psx_combat_active')==1,'original combat must be active')
        require(s.get('psx_round_flow_active')==1,'original round controller must be active')
        require(s['first_error']==0 and s['draw_timeouts']==0 and s['psx_attack_decode_errors']==[0,0],
                name+': runtime or decode error')
        require(s['faces_rejected']==0 and s['commands_used']<=s['commands_capacity'],
                name+': scene or command capacity exceeded')
        require(s['psx_skin_preview_applied']==[1,1],name+': both original actors must render')
        return s
    close='20 1 buttons=RIGHT\n100 1 buttons=NONE\n'
    neutral=case('neutral')
    require(neutral['psx_health_q16']==[140*65536]*2,'neutral must preserve HP')
    for name,attack,defense,slot in [('high-hit','X','LEFT',2250),
            ('mid-hit','RIGHT+DOWN+X','DOWN',2328),('low-hit','DOWN+A','NONE',2352)]:
        s=case(name,close+f'110 2 buttons={defense}\n120 1 buttons={attack}\n')
        require(s['psx_health_q16'][0]==140*65536 and 0<s['psx_health_q16'][1]<140*65536,
                name+': only defender must lose HP')
        require(s['psx_registered_hits']==[0,1] and s['psx_registered_blocks']==[0,0],
                name+': one held attack must register once')
        require(s['psx_reaction_slot'][1]==slot,name+': wrong original reaction')
    for name,attack,defense,slot in [('standing-guard','X','NONE',2693),('crouching-guard','DOWN+A','DOWN',2739)]:
        s=case(name,close+f'110 2 buttons={defense}\n120 1 buttons={attack}\n')
        require(s['psx_health_q16']==[140*65536]*2 and s['psx_registered_blocks']==[0,1]
                and s['psx_registered_hits']==[0,0],name+': guard must prevent damage')
        require(s['psx_reaction_slot'][1]==slot,name+': wrong guard reaction')
    s=case('visible-reaction',close+'110 2 buttons=LEFT\n120 1 buttons=X\n',155)
    require(s['psx_move_slot'][1]==2250 and s['psx_move_frame'][1]>1,'reaction must own the visible VM')
    s=case('miss','20 1 buttons=LEFT\n100 1 buttons=NONE\n120 1 buttons=X\n')
    require(s['psx_health_q16']==[140*65536]*2 and s['psx_registered_hits']==[0,0]
            and s['psx_registered_blocks']==[0,0],'far high must miss')
    script=close+'110 2 buttons=LEFT\n120 1 buttons=X\n'
    held=case('held-high',script,420)
    repressed=case('repressed-high',script+'210 1 buttons=NONE\n260 1 buttons=X\n',420)
    require(sum(held['psx_registered_hits']+held['psx_registered_blocks'])==1,'held high repeated a hit')
    require(sum(repressed['psx_registered_hits']+repressed['psx_registered_blocks'])==2,'release/repress must register another contact')
    s=case('second-pad','20 2 buttons=LEFT\n100 2 buttons=NONE\n110 1 buttons=RIGHT\n120 2 buttons=X\n')
    require(0<s['psx_health_q16'][0]<140*65536 and s['psx_health_q16'][1]==140*65536
            and s['psx_registered_hits']==[1,0],'P2 must independently damage P1')
    s=case('simultaneous','20 1 buttons=RIGHT\n20 2 buttons=LEFT\n100 1 buttons=NONE\n100 2 buttons=NONE\n120 1 buttons=X\n120 2 buttons=X\n')
    require(all(0<h<140*65536 for h in s['psx_health_q16']) and s['psx_registered_hits']==[1,1],
            'simultaneous attacks must publish both damage results')
    s=case('unsupported-move',close+'120 1 buttons=Y\n')
    require(s['psx_unsupported_moves'][0]>0 and s['psx_health_q16']==[140*65536]*2,
            'unsupported commands must be reported without invented combat')
    s=case('reset',script+'390 2 buttons=NONE\n400 1 buttons=START\n420 1 buttons=NONE\n',500)
    require(s['psx_health_q16']==[140*65536]*2 and s['psx_registered_hits']==[0,0]
            and s['psx_registered_blocks']==[0,0] and s['psx_anchor'][::3]==[-1000,1000],
            'START must reset native HP, hits and placement')
    if args.with_ko:
        # Observe the side before each approach. A fixed LEFT/RIGHT list from
        # an older collision model can miss after actors exchange sides.
        # Every observation reruns the full pad prefix from reset; no guest
        # state is edited and no damage or round assertion is relaxed.
        lines=[]
        def finish_round(label,start,state,wins):
            for i in range(22):
                roots=state['psx_world_root']
                direction='RIGHT' if roots[0]<roots[3] else 'LEFT'
                t=start+180*i
                lines.extend([f'{t} 1 buttons={direction}',f'{t+60} 1 buttons=NONE',
                              f'{t+68} 1 buttons=DOWN+A',f'{t+138} 1 buttons=NONE'])
                name=f'{label}-approach-{i}'
                state=case(name,'\n'.join(lines)+'\n',t+175)
                results.pop(name)
                if state['wins0']==wins and state['round_done']:
                    return state,t+175
            raise AssertionError(label+': controlled lows did not finish the round')
        s,end=finish_round('ko',20,neutral,1)
        tape='\n'.join(lines)+'\n'
        # The approach that ends the round is sampled when it is already
        # over; a faster build has run past the collapse start by then. Walk
        # the sample point back until the collapse move itself is visible.
        for back in range(0,160,10):
            s=case('ko-collapse',tape,end-back)
            if s['psx_ko_collapse'][1]==1 and s['psx_move_slot'][1]==3430:
                break
        require(s['psx_health_q16'][1]==0 and s['psx_ko_collapse'][1]==1
                and s['psx_move_slot'][1]==3430,'KO must start the original collapse move')
        s=case('ko-collapse',tape,end)
        require(s['round_done']==1 and s['winner']==0 and s['wins0']==1 and not s['match_done'],
                'KO must award P1 exactly one round')
        replay=case('ko-replay',tape,end+240)
        require(replay['psx_round_state']==4 and replay['psx_replay_mode'] in (1,3)
                and replay['wins0']==1 and replay['psx_health_q16'][1]==0,
                'KO must play the native history before victory')
        victory=None;victory_end=0
        for offset in range(300,1801,60):
            name='victory-search-'+str(offset)
            observed=case(name,tape,end+offset);results.pop(name)
            if observed['psx_round_state']==6:
                victory=observed;victory_end=end+offset;break
        require(victory is not None,'native replay must restore the live fight and start victory')
        victory=case('ko-victory',tape,victory_end)
        require(victory['psx_round_state']==6 and victory['psx_replay_mode']==5 and victory['psx_move_slot'][0] in range(3431,3435)
                and victory['psx_move_frame'][0]>1,'victory must own an authored pose after replay')
        held_victory=case('ko-victory-hold',tape,victory_end+60)
        require(held_victory['psx_round_state']==6
                and held_victory['psx_move_frame'][0]>victory['psx_move_frame'][0]
                and held_victory['psx_move_frame'][1]==victory['psx_move_frame'][1],
                'winner pose must advance while the loser clock stays frozen')
        next_end=0
        # A fixed post-KO wait can consume much of round two as throughput
        # improves. Begin its controlled attacks at the first observed fresh
        # round, rather than accidentally turning the second KO into timeout.
        for checkpoint in range(victory_end+60,victory_end+1801,60):
            name='next-round-search-'+str(checkpoint)
            observed=case(name,tape,checkpoint);results.pop(name)
            if observed['round']==2 and not observed['round_done']:
                next_end=checkpoint;break
        require(next_end>0,'victory must reach a fresh second round')
        s=case('next-round',tape,next_end)
        require(s['round']==2 and not s['round_done'] and s['wins0']==1 and s['wins1']==0
                and s['psx_health_q16']==[140*65536]*2 and s['psx_anchor'][::3]==[-1000,1000]
                and s['psx_registered_hits']==[0,0] and s['psx_ko_collapse']==[0,0],
                'next round must retain score and restore fighters, HP and hit state')
        s,match_end=finish_round('match',next_end+20,s,2)
        match_tape='\n'.join(lines)+'\n'
        s=case('match-end',match_tape,match_end)
        require(s['round']==2 and s['round_done'] and s['match_done'] and s['winner']==0
                and s['wins0']==2 and s['wins1']==0 and s['psx_health_q16'][1]==0,
                'second KO must end the first-to-two match exactly once')
        terminal_end=match_end+1600
        finished=case('match-finished',match_tape,terminal_end)
        require(finished['psx_round_state']==9 and finished['psx_replay_mode']==5
                and finished['match_done'] and finished['wins0']==2 and finished['wins1']==0,
                'terminal match must complete replay and victory without starting another round')
        s=case('match-reset',match_tape+
               f'{terminal_end+40} 1 buttons=START\n{terminal_end+60} 1 buttons=NONE\n',terminal_end+200)
        require(s['round']==1 and not s['round_done'] and not s['match_done']
                and s['wins0']==s['wins1']==0 and s['psx_health_q16']==[140*65536]*2
                and s['psx_replay_epoch']>finished['psx_replay_epoch'],
                'Start must restore a fresh match after match end')
    if getattr(args,'with_timeout',False):
        # Neutral connected pads, no guest RAM edits or accelerated game clock.
        timeout_start=0
        for checkpoint in range(2400,7201,600):
            name='timeout-round-search-'+str(checkpoint)
            observed=case(name,'',checkpoint);results.pop(name)
            if observed['round']==2 and not observed['match_done']:
                timeout_start=checkpoint;break
        require(timeout_start>0,'first neutral timeout must reach round two')
        s=case('timeout-next-round','',timeout_start)
        require(s['round']==2 and s['wins0']==s['wins1']==1 and not s['match_done']
                and s['psx_health_q16']==[140*65536]*2,
                'first neutral draw must award both players and restart')
        s=case('timeout-drawn-match','',12000)
        require(s['round']==2 and s['wins0']==s['wins1']==2 and s['match_done']
                and s['round_done'] and s['winner']==-1 and s['timer']==0
                and s['psx_health_q16']==[140*65536]*2,
                'second neutral draw must finish at 2-2 and report a drawn match')
    report=dict(passed=True,bounded_round_policy_validated=False,
                input_offset_fields=offset,input_offset_measured=measured,
                native_round_replay_victory_validated=bool(args.with_ko),
                original_vs_timeout_scoring_validated=bool(getattr(args,'with_timeout',False)),
                original_round_flow_parity=False,ps1_input_replay_parity=False,cases=results)
    (root/'suite.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(results)} live original-combat cases passed; {root / "suite.json"}')


if __name__=='__main__':
    parser=saturn_probe.parser();parser.add_argument('--with-ko',action='store_true')
    parser.add_argument('--input-offset-fields',type=int,default=None,
                        help='delay all inputs and endpoints by this many fields; default measures the first simulation tick')
    parser.add_argument('--with-timeout',action='store_true',help='Run two neutral timeout scoring/restart checkpoints')
    parser.set_defaults(build=saturn_probe.ROOT/'build/paul-combat',
                        out=saturn_probe.ROOT/'assets/imported/saturn-original-combat-suite')
    execute(parser.parse_args())
