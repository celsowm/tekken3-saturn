#!/usr/bin/env python3
"""Rebuild an optimized Saturn configuration in a fresh directory."""
import argparse,json,os,subprocess
from pathlib import Path
from run_saturn_stage_matrix import ROOT,cache_options,project_overrides
import saturn_workload


def build(baseline,out,overrides,workload='diagnostic',sdk_prefix=None):
    options,compiler_dir=cache_options(baseline.resolve()/'CMakeCache.txt')
    overrides=project_overrides(overrides)
    if sdk_prefix is not None:
        package=sdk_prefix.resolve()/'lib/cmake/LibSaturn'
        if not (package/'LibSaturnConfig.cmake').is_file():
            raise FileNotFoundError('SDK prefix has no installed LibSaturn CMake package: '+str(package))
        options.append('-DLibSaturn_DIR:PATH='+str(package))
    out=out.resolve()
    if out.exists() and any(out.iterdir()):
        raise FileExistsError('use a fresh empty build directory')
    out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ);env['PATH']=str(compiler_dir)+os.pathsep+env.get('PATH','')
    commands=[['cmake','-S',str(ROOT),'-B',str(out),'-G','Ninja',*options,*overrides],
              ['cmake','--build',str(out),'--parallel','4']]
    manifest=dict(baseline=str(baseline.resolve()),commands=commands,completed=False)
    (out/'variant.json').write_text(json.dumps(manifest,indent=2)+'\n')
    for i,command in enumerate(commands):
        with (out/f'variant-step-{i}.log').open('w') as log:
            subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    identity=saturn_workload.inspect(out)
    saturn_workload.validate(identity,workload)
    manifest['workload_profile']=workload
    manifest['workload']=identity
    manifest['completed']=True
    (out/'variant.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cmake-option',action='append',default=[])
    p.add_argument('--workload',choices=saturn_workload.PROFILES,default='diagnostic')
    p.add_argument('--sdk-prefix',type=Path,help='explicit installed SDK for a matched library control')
    a=p.parse_args();build(a.baseline,a.out,a.cmake_option,a.workload,a.sdk_prefix)
