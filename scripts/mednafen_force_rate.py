#!/usr/bin/env python3
"""Run a Tekken Force disc (a T3_FORCE_INPUT_PROBE build) for N seconds in an isolated Mednafen profile and print the
rate counters the probe leaves at tick 600 (g_force_probe_600: input hash, state hash, fields, observed fields,
discarded fields, scheduled ticks, images, pose cache hits/misses/evictions, worst fields between two images) from
the exit save state.
No recording: the cost is the save state only. Run it on the candidate and the control build and compare.

    scripts/mednafen_force_rate.py --build build/force-work --out build/force-rate-a --seconds 70
"""
import argparse
import os
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIOS = ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin'

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--seconds', type=float, default=70)
p.add_argument('--mednafen', default='mednafen')
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True)
profile = out / 'mednafen-profile'
profile.mkdir()
cue = (a.build / 'disc/tekken3_force.cue').resolve()
command = [a.mednafen, '-sound', '0', '-video.driver', 'softfb', '-ss.region_default', 'na',
           '-ss.bios_na_eu', str(BIOS), '-autosave', '1', str(cue)]
log = (out / 'mednafen.log').open('w')
process = subprocess.Popen(command, env={**os.environ, 'MEDNAFEN_HOME': str(profile)}, stdout=log, stderr=subprocess.STDOUT)
time.sleep(a.seconds)
process.send_signal(signal.SIGINT)
process.wait(timeout=30)
print(subprocess.check_output(['python3', str(ROOT / 'scripts/mednafen_state.py'), str(out), '--elf',
                               str(a.build / 'tekken3_force.elf'), '--symbol', '_g_force_probe_600', '--words'],
                              text=True))
