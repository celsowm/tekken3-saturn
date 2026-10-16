#!/usr/bin/env python3
"""Compare recorded/replayed Saturn inputs and observed state at one logic tick.

This never certifies PS1 parity or unobserved intermediate ticks.
"""
import argparse,json
from pathlib import Path

GAME_FIELDS=('fighter_x','fighter_z','health','round','round_done','match_done','winner',
             'wins0','wins1','timer','hits','blocks','sim_ticks')

def compare(record,replay):
    original=json.loads((record/'observation.json').read_text())
    actual=json.loads((replay/'observation.json').read_text())
    required=('sim_ticks','psx_owned_active','psx_combat_active','psx_move_slot',
              'psx_move_frame','psx_health_q16','psx_anchor','round','round_done','wins0','wins1')
    for result in (original,actual):
        if any(name not in result['state'] for name in required):
            raise ValueError('missing required owned-combat observations')
        if result['state']['psx_owned_active']!=1 or result['state']['psx_combat_active']!=1:
            raise ValueError('comparison requires owned original combat')
    fields=[name for name in original['state'] if name.startswith('psx_') or name in GAME_FIELDS]
    differences=[name for name in fields if original['state'][name]!=actual['state'].get(name)]
    first=json.loads((record/'input-tape.json').read_text())
    second=json.loads((replay/'input-tape.json').read_text())
    if first['final_tick']!=original['state']['sim_ticks'] or second['final_tick']!=actual['state']['sim_ticks']:
        raise ValueError('input trace endpoint differs from observed simulation tick')
    inputs_match=first==second
    report=dict(passed=not differences and inputs_match,fields=fields,differences=differences,
                inputs_match=inputs_match,input_events=len(first['events']),final_tick=first['final_tick'],
                source_elf_sha256=original['elf_sha256'],replay_elf_sha256=actual['elf_sha256'],
                scope='Free-running Saturn recording/replay at matched tick from reset; '
                      'not every intermediate state or PS1 replay parity')
    (replay/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return report['passed']

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--record',type=Path,required=True);p.add_argument('--replay',type=Path,required=True)
    a=p.parse_args();raise SystemExit(not compare(a.record,a.replay))
